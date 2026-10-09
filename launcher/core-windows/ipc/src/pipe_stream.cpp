#include "pipe_stream.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace rb::os_windows::ipc {
namespace {

constexpr DWORD kReadChunk = 64 * 1024;
// WriteFile takes a DWORD; larger queues go out in several calls.
constexpr std::size_t kMaxWrite = std::size_t{1} << 30;

// The peer closed its end: nothing more can be sent, but what it wrote first is still readable.
[[nodiscard]] bool peer_gone(DWORD error) noexcept {
    return error == ERROR_NO_DATA || error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED;
}

}  // namespace

// Every member is guarded by `mutex`. Each request in flight holds a keep, so the Impl outlives
// its packets; `io` is cleared once the thread may no longer be used.
struct PipeStream::Impl final : PipeIoThread::Owner, std::enable_shared_from_this<Impl> {
    Impl(UniqueHandle handle, ports::PeerIdentity identity, PipeIoThread& thread)
        : peer(std::move(identity)), io(&thread), pipe(std::move(handle)), read_buffer(kReadChunk) {}

    void abandon() noexcept override {
        std::unique_lock lock{mutex};
        finish();
        settle(lock);
        io = nullptr;
    }

    void start_read() {
        if (finished || reading || detached || !on_read) return;
        if (!io->begin_io()) {
            finish();
            return;
        }
        reading = true;
        read_keep = shared_from_this();
        read_request.overlapped = OVERLAPPED{};
        // A synchronous success still queues its packet, so only a failure is handled here.
        if (!ReadFile(pipe.get(), read_buffer.data(), kReadChunk, nullptr, &read_request.overlapped) &&
            GetLastError() != ERROR_IO_PENDING) {
            io->end_io();
            reading = false;
            read_keep.reset();
            finish();
        }
    }

    void start_write() {
        if (finished || writing || send_closed) return;
        if (sent == sending.size()) {
            if (queued.empty()) return;
            sending.swap(queued);
            queued.clear();
            sent = 0;
        }
        if (!io->begin_io()) {
            finish();
            return;
        }
        writing = true;
        write_keep = shared_from_this();
        write_request.overlapped = OVERLAPPED{};
        const auto size = static_cast<DWORD>(std::min(sending.size() - sent, kMaxWrite));
        if (!WriteFile(pipe.get(), sending.data() + sent, size, nullptr, &write_request.overlapped)) {
            const DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) return;
            io->end_io();
            writing = false;
            write_keep.reset();
            fail_send(error);
        }
    }

    // close() hands what is queued behind the write in flight to the pipe as one more write;
    // whatever the pipe cannot take without waiting is cancelled with the rest.
    void flush_queued() {
        if (finished || send_closed || flushing || queued.empty() || io == nullptr) return;
        if (!io->begin_io()) return;
        flushing = true;
        flush_keep = shared_from_this();
        flush_bytes.swap(queued);
        queued.clear();
        flush_request.overlapped = OVERLAPPED{};
        const auto size = static_cast<DWORD>(std::min(flush_bytes.size(), kMaxWrite));
        if (!WriteFile(pipe.get(), flush_bytes.data(), size, nullptr, &flush_request.overlapped) &&
            GetLastError() != ERROR_IO_PENDING) {
            io->end_io();
            flushing = false;
            flush_keep.reset();
        }
    }

    void fail_send(DWORD error) {
        if (!peer_gone(error)) {
            finish();
            return;
        }
        send_closed = true;
        queued.clear();
        if (!writing) {
            sending.clear();
            sent = 0;
        }
    }

    // Closing the handle completes every pending request with an error.
    void finish() {
        if (finished) return;
        finished = true;
        // A buffer an overlapped write still uses stays until its completion.
        if (!writing) {
            sending.clear();
            sent = 0;
        }
        queued.clear();
        CancelIoEx(pipe.get(), nullptr);
        pipe.reset();
    }

    // Fires on_close once the stream finished: inline on the I/O thread or once the thread is
    // gone, otherwise through a posted packet so it still runs there.
    void settle(std::unique_lock<std::mutex>& lock) {
        if (!finished || detached || close_fired || close_posted || !on_close) return;
        if (io != nullptr && !io->on_thread()) {
            close_posted = true;
            close_keep = shared_from_this();
            if (io->post(close_request)) return;
            close_posted = false;
            close_keep.reset();
        }
        close_fired = true;
        UniqueFunction<void()> callback = std::move(on_close);
        enter_callback();
        lock.unlock();
        callback();
        callback = {};
        lock.lock();
        leave_callback();
    }

    void enter_callback() { running.push_back(GetCurrentThreadId()); }

    void leave_callback() {
        running.erase(std::ranges::find(running, GetCurrentThreadId()));
        idle.notify_all();
    }

    // No callback runs on another thread; one on `thread` is the caller's own.
    [[nodiscard]] bool idle_for(DWORD thread) const {
        return std::ranges::all_of(running, [thread](DWORD id) { return id == thread; });
    }

    static void read_done(void* owner, DWORD bytes, DWORD error) {
        Impl& self = *static_cast<Impl*>(owner);
        std::shared_ptr<Impl> keep;
        UniqueFunction<void(std::span<const u8>)> callback;
        std::unique_lock lock{self.mutex};
        keep = std::move(self.read_keep);
        if (!self.finished && error != ERROR_SUCCESS) self.finish();
        // `reading` stays set while the callback runs, so no read refills the buffer it was given.
        if (!self.finished && bytes != 0 && !self.detached && self.on_read) {
            callback = std::move(self.on_read);
            self.enter_callback();
            lock.unlock();
            callback(std::span<const u8>{self.read_buffer.data(), bytes});
            lock.lock();
            self.leave_callback();
            // A callback set meanwhile wins over the one that just ran.
            if (!self.on_read && !self.detached) self.on_read = std::move(callback);
        }
        self.reading = false;
        self.start_read();
        self.settle(lock);
    }

    static void write_done(void* owner, DWORD bytes, DWORD error) {
        Impl& self = *static_cast<Impl*>(owner);
        std::shared_ptr<Impl> keep;
        std::unique_lock lock{self.mutex};
        keep = std::move(self.write_keep);
        self.writing = false;
        if (!self.finished && error != ERROR_SUCCESS) self.fail_send(error);
        if (self.finished || self.send_closed) {
            self.sending.clear();
            self.sent = 0;
        } else {
            self.sent += bytes;
            if (self.sent == self.sending.size()) {
                self.sending.clear();
                self.sent = 0;
            }
        }
        self.start_write();
        self.settle(lock);
    }

    static void flush_done(void* owner, DWORD, DWORD) {
        Impl& self = *static_cast<Impl*>(owner);
        std::shared_ptr<Impl> keep;
        std::unique_lock lock{self.mutex};
        keep = std::move(self.flush_keep);
        self.flushing = false;
        self.flush_bytes.clear();
        self.settle(lock);
    }

    static void close_done(void* owner, DWORD, DWORD) {
        Impl& self = *static_cast<Impl*>(owner);
        std::shared_ptr<Impl> keep;
        std::unique_lock lock{self.mutex};
        keep = std::move(self.close_keep);
        self.close_posted = false;
        self.settle(lock);
    }

    const ports::PeerIdentity peer;

    std::mutex mutex;
    std::condition_variable idle;
    PipeIoThread* io;
    UniqueHandle pipe;
    PipeIoThread::Request read_request{OVERLAPPED{}, this, &read_done};
    PipeIoThread::Request write_request{OVERLAPPED{}, this, &write_done};
    PipeIoThread::Request flush_request{OVERLAPPED{}, this, &flush_done};
    PipeIoThread::Request close_request{OVERLAPPED{}, this, &close_done};
    std::shared_ptr<Impl> read_keep;
    std::shared_ptr<Impl> write_keep;
    std::shared_ptr<Impl> flush_keep;
    std::shared_ptr<Impl> close_keep;
    std::vector<u8> read_buffer;
    // The bytes of the write in flight; `sent` of them are already through.
    std::vector<u8> sending;
    std::size_t sent = 0;
    std::vector<u8> queued;
    // The last write, issued by close().
    std::vector<u8> flush_bytes;
    UniqueFunction<void(std::span<const u8>)> on_read;
    UniqueFunction<void()> on_close;
    bool reading = false;
    bool writing = false;
    bool flushing = false;
    // The peer hung up: writes are dropped while reading runs to the end of what it sent.
    bool send_closed = false;
    bool finished = false;
    bool close_posted = false;
    bool close_fired = false;
    // The PipeStream is gone: no callback runs any more.
    bool detached = false;
    // The thread of each callback running now; the destructor waits for those on other threads.
    std::vector<DWORD> running;
};

Result<std::unique_ptr<PipeStream>> PipeStream::accepted(UniqueHandle pipe, ports::PeerIdentity peer, PipeIoThread& io) {
    auto impl = std::make_shared<Impl>(std::move(pipe), std::move(peer), io);
    io.attach(impl);
    return std::unique_ptr<PipeStream>(new PipeStream(std::move(impl), nullptr));
}

Result<std::unique_ptr<PipeStream>> PipeStream::connected(UniqueHandle pipe, ports::PeerIdentity peer) {
    Result<std::unique_ptr<PipeIoThread>> io = PipeIoThread::start();
    if (!io) return std::unexpected(std::move(io.error()));
    if (Result<void> bound = (*io)->bind(pipe.get()); !bound) return std::unexpected(std::move(bound.error()));
    auto impl = std::make_shared<Impl>(std::move(pipe), std::move(peer), **io);
    (*io)->attach(impl);
    return std::unique_ptr<PipeStream>(new PipeStream(std::move(impl), std::move(*io)));
}

PipeStream::PipeStream(std::shared_ptr<Impl> impl, std::unique_ptr<PipeIoThread> owned_io)
    : impl_(std::move(impl)), owned_io_(std::move(owned_io)) {}

PipeStream::~PipeStream() {
    UniqueFunction<void(std::span<const u8>)> read_callback;
    UniqueFunction<void()> close_callback;
    {
        std::unique_lock lock{impl_->mutex};
        impl_->detached = true;
        read_callback = std::move(impl_->on_read);
        close_callback = std::move(impl_->on_close);
        impl_->finish();
        if (impl_->io != nullptr) {
            impl_->io->detach(impl_.get());
            impl_->io = nullptr;
        }
        // Also after the listener's thread abandoned the stream, whose close packet may still run.
        const DWORD self = GetCurrentThreadId();
        impl_->idle.wait(lock, [this, self] { return impl_->idle_for(self); });
    }
    owned_io_.reset();
}

void PipeStream::write(std::span<const u8> bytes) {
    if (bytes.empty()) return;
    std::unique_lock lock{impl_->mutex};
    if (impl_->finished || impl_->send_closed) return;
    impl_->queued.insert(impl_->queued.end(), bytes.begin(), bytes.end());
    impl_->start_write();
    impl_->settle(lock);
}

void PipeStream::on_read(UniqueFunction<void(std::span<const u8>)> callback) {
    std::unique_lock lock{impl_->mutex};
    std::swap(impl_->on_read, callback);
    impl_->start_read();
    impl_->settle(lock);
}

void PipeStream::on_close(UniqueFunction<void()> callback) {
    std::unique_lock lock{impl_->mutex};
    std::swap(impl_->on_close, callback);
    impl_->settle(lock);
}

void PipeStream::close() {
    std::unique_lock lock{impl_->mutex};
    impl_->flush_queued();
    impl_->finish();
    impl_->settle(lock);
}

ports::PeerIdentity PipeStream::peer() const { return impl_->peer; }

}  // namespace rb::os_windows::ipc
