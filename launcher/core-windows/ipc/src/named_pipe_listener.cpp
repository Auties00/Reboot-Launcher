#include "reboot/os_windows/ipc/named_pipe_listener.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "pipe_io_thread.hpp"
#include "pipe_stream.hpp"
#include "reboot/foundation/log.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"
#include "win32.hpp"
#include "win32_errors.hpp"

namespace reboot::os_windows::ipc {
namespace {

constexpr DWORD kPipeBuffer = 64 * 1024;

}  // namespace

// Shared with the accept request in flight, so a packet arriving after the listener is gone
// still finds it.
struct NamedPipeListener::Impl : std::enable_shared_from_this<Impl> {
    explicit Impl(PipeTrust pipe_trust) : trust(std::move(pipe_trust)) {}

    [[nodiscard]] Result<UniqueHandle> create_instance(bool first) {
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security.data(), FALSE};
        const DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
        UniqueHandle pipe{CreateNamedPipeW(name.c_str(), open_mode,
                                           PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                           PIPE_UNLIMITED_INSTANCES, kPipeBuffer, kPipeBuffer, 0, &attributes)};
        if (!pipe) {
            const DWORD error = GetLastError();
            if (first && (error == ERROR_ACCESS_DENIED || error == ERROR_PIPE_BUSY))
                return std::unexpected(
                    make_diag(ErrorDomain::Platform, kPipeNameTaken).arg("name", endpoint).kind(ErrorKind::Conflict).build());
            return std::unexpected(pipe_call_failed("CreateNamedPipeW", endpoint, error));
        }
        if (Result<void> bound = io->bind(pipe.get()); !bound) return std::unexpected(std::move(bound.error()));
        return pipe;
    }

    // Starts waiting for the next client on `waiting`; a client already connected is handed to
    // the I/O thread through a posted packet.
    void arm() {
        while (!closed && waiting && !accept_pending) {
            if (!io->begin_io()) return;
            accept_request.overlapped = OVERLAPPED{};
            accept_keep = shared_from_this();
            if (ConnectNamedPipe(waiting.get(), &accept_request.overlapped)) {
                accept_pending = true;
                return;
            }
            const DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                accept_pending = true;
                return;
            }
            io->end_io();
            if (error == ERROR_PIPE_CONNECTED) {
                accept_pending = io->post(accept_request);
                if (!accept_pending) accept_keep.reset();
                return;
            }
            accept_keep.reset();
            if (error != ERROR_NO_DATA) {
                REBOOT_LOG_ERROR(Ipc, "the engine pipe {} stopped accepting: ConnectNamedPipe failed with {}", endpoint, error);
                waiting.reset();
                return;
            }
            // The client connected and left already; this instance cannot be reused without a disconnect.
            DisconnectNamedPipe(waiting.get());
        }
    }

    static void accept_done(void* owner, DWORD, DWORD error) {
        Impl& self = *static_cast<Impl*>(owner);
        std::shared_ptr<Impl> keep;
        UniqueHandle client;
        {
            const std::lock_guard lock{self.mutex};
            keep = std::move(self.accept_keep);
            self.accept_pending = false;
            if (self.closed) return;
            if (error == ERROR_SUCCESS || error == ERROR_PIPE_CONNECTED) {
                client = std::move(self.waiting);
                // A fresh instance waits before this client is verified, so the name never lapses.
                Result<UniqueHandle> next = self.create_instance(false);
                if (next) {
                    self.waiting = std::move(*next);
                } else {
                    REBOOT_LOG_ERROR(Ipc, "the engine pipe {} stopped accepting: {}", self.endpoint, next.error().id);
                }
            } else {
                DisconnectNamedPipe(self.waiting.get());
            }
            self.arm();
        }
        if (client) self.hand_over(std::move(client));
    }

    void hand_over(UniqueHandle client) {
        Result<ports::PeerIdentity> peer = trust.verify_client(client.get());
        if (!peer) {
            const Diagnostic& error = peer.error();
            REBOOT_LOG_WARN(Ipc, "dropped a client of the engine pipe: {} ({})", error.id,
                            error.causes.empty() ? std::string{} : error.causes.front().id);
            return;
        }
        Result<std::unique_ptr<PipeStream>> stream = PipeStream::accepted(std::move(client), std::move(*peer), *io);
        if (!stream) {
            REBOOT_LOG_ERROR(Ipc, "an engine pipe client could not be served: {}", stream.error().id);
            return;
        }
        if (closed_flag.load()) return;
        on_accept(std::move(*stream));
    }

    void close() {
        const std::lock_guard lock{mutex};
        if (closed) return;
        closed = true;
        closed_flag = true;
        if (waiting) {
            CancelIoEx(waiting.get(), nullptr);
            waiting.reset();
        }
    }

    PipeTrust trust;
    std::wstring name;
    std::string endpoint;
    std::vector<u8> security;
    // Runs on the I/O thread only, once listen() returned.
    UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> on_accept;

    std::mutex mutex;
    UniqueHandle waiting;
    PipeIoThread::Request accept_request{OVERLAPPED{}, this, &accept_done};
    std::shared_ptr<Impl> accept_keep;
    bool accept_pending = false;
    bool listening = false;
    bool closed = false;
    // `closed` for the hand-over, which runs unlocked.
    std::atomic<bool> closed_flag{false};
    std::unique_ptr<PipeIoThread> io_owner;
    // Stays set while the thread is torn down, since only the thread itself uses it then.
    PipeIoThread* io = nullptr;
};

NamedPipeListener::NamedPipeListener(PipeTrust trust) : impl_(std::make_shared<Impl>(std::move(trust))) {}

NamedPipeListener::~NamedPipeListener() {
    impl_->close();
    // Abandons the accepted streams and runs their close packets before the thread ends.
    impl_->io_owner.reset();
}

Result<void> NamedPipeListener::listen(std::string_view endpoint_name,
                                       UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> on_accept) {
    Impl& impl = *impl_;
    const std::lock_guard lock{impl.mutex};
    if (impl.listening || impl.closed) return std::unexpected(internal_bug("os_windows.named_pipe_listener.listen_again"));
    impl.endpoint = std::string(endpoint_name);
    impl.name = to_wide(endpoint_name);
    Result<std::vector<u8>> security = impl.trust.pipe_security_descriptor();
    if (!security) return std::unexpected(std::move(security.error()));
    impl.security = std::move(*security);
    Result<std::unique_ptr<PipeIoThread>> io = PipeIoThread::start();
    if (!io) return std::unexpected(std::move(io.error()));
    impl.io_owner = std::move(*io);
    impl.io = impl.io_owner.get();
    Result<UniqueHandle> first = impl.create_instance(true);
    if (!first) {
        impl.io = nullptr;
        impl.io_owner.reset();
        return std::unexpected(std::move(first.error()));
    }
    impl.waiting = std::move(*first);
    impl.on_accept = std::move(on_accept);
    impl.listening = true;
    impl.arm();
    return {};
}

void NamedPipeListener::close() { impl_->close(); }

}  // namespace reboot::os_windows::ipc
