#include "socket_stream.hpp"

#include <array>
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <system_error>
#include <utility>

#include "reboot/foundation/log.hpp"
#include "reboot/posix/posix_error.hpp"

namespace reboot::posix {

namespace {

constexpr std::size_t kReadChunk = 64 * 1024;

}  // namespace

Result<std::unique_ptr<SocketStream>> SocketStream::start(UniqueFd socket, ports::PeerIdentity peer) {
    auto wake = WakePipe::create();
    if (!wake) return std::unexpected(std::move(wake.error()));
    std::unique_ptr<SocketStream> stream{new SocketStream(std::move(socket), std::move(peer), std::move(*wake))};
    try {
        stream->thread_ = std::thread([raw = stream.get()] { raw->run(); });
    } catch (const std::system_error& error) {
        return std::unexpected(call_failed("pthread_create", error.code().value()));
    }
    return stream;
}

SocketStream::SocketStream(UniqueFd socket, ports::PeerIdentity peer, WakePipe wake)
    : socket_(std::move(socket)), peer_(std::move(peer)), wake_(std::move(wake)), read_buffer_(kReadChunk) {}

SocketStream::~SocketStream() {
    {
        const std::lock_guard lock{mutex_};
        stopping_ = true;
    }
    wake_.wake();
    if (thread_.joinable()) thread_.join();
}

void SocketStream::write(std::span<const u8> bytes) {
    const std::lock_guard lock{mutex_};
    if (finished_ || send_closed_ || bytes.empty()) return;
    if (!outbound_.empty()) {
        outbound_.insert(outbound_.end(), bytes.begin(), bytes.end());
        return;
    }
    const std::size_t sent = send_locked(bytes);
    if (finished_ || send_closed_ || sent == bytes.size()) return;
    outbound_.assign(bytes.begin() + static_cast<std::ptrdiff_t>(sent), bytes.end());
    wake_.wake();
}

void SocketStream::on_read(UniqueFunction<void(std::span<const u8>)> callback) {
    const std::lock_guard lock{mutex_};
    on_read_ = std::move(callback);
    wake_.wake();
}

void SocketStream::on_close(UniqueFunction<void()> callback) {
    const std::lock_guard lock{mutex_};
    on_close_ = std::move(callback);
    wake_.wake();
}

void SocketStream::close() {
    const std::lock_guard lock{mutex_};
    if (finished_) return;
    flush_locked();
    finish_locked();
}

ports::PeerIdentity SocketStream::peer() const { return peer_; }

void SocketStream::run() {
    try {
        while (run_once()) {
        }
    } catch (...) {
        try {
            REBOOT_LOG_ERROR(Ipc, "{} in the engine socket stream thread", internal_bug("posix.socket_stream").id);
            {
                const std::lock_guard lock{mutex_};
                finish_locked();
            }
            fire_close_once();
        } catch (...) {
        }
    }
}

bool SocketStream::run_once() {
    std::array<pollfd, 2> fds{};
    fds[0] = pollfd{.fd = wake_.read_fd(), .events = POLLIN, .revents = 0};
    bool close_due = false;
    {
        const std::lock_guard lock{mutex_};
        if (stopping_) return false;
        close_due = finished_ && !close_fired_ && on_close_;
        short events = 0;
        if (!finished_ && on_read_) events |= POLLIN;
        if (!finished_ && !hung_up_ && !outbound_.empty()) events |= POLLOUT;
        fds[1] = pollfd{.fd = events != 0 ? socket_.get() : -1, .events = events, .revents = 0};
    }
    if (close_due) {
        fire_close_once();
        return true;
    }

    if (::poll(fds.data(), fds.size(), -1) < 0) {
        if (errno == EINTR) return true;
        throw std::system_error(errno, std::generic_category(), "poll");
    }
    if (fds[0].revents != 0) wake_.drain();

    const short ready = fds[1].revents;
    if ((ready & POLLNVAL) != 0) {
        const std::lock_guard lock{mutex_};
        finish_locked();
        return true;
    }
    if ((ready & POLLOUT) != 0) {
        const std::lock_guard lock{mutex_};
        flush_locked();
    }
    if ((ready & (POLLIN | POLLHUP | POLLERR)) != 0) {
        bool reading = false;
        {
            const std::lock_guard lock{mutex_};
            // close() from another thread may have finished the stream since poll returned.
            if (finished_) return true;
            reading = static_cast<bool>(on_read_);
            if (!reading) hung_up_ = true;
        }
        if (reading) read_available();
    }
    return true;
}

void SocketStream::read_available() {
    const IoResult got = receive_some(socket_.get(), read_buffer_);
    if (got.would_block) return;
    if (got.bytes == 0) {
        const std::lock_guard lock{mutex_};
        finish_locked();
        return;
    }
    UniqueFunction<void(std::span<const u8>)> callback;
    {
        const std::lock_guard lock{mutex_};
        callback = std::move(on_read_);
    }
    // Invoked unlocked, since the callback may write; a replacement set meanwhile wins.
    if (callback) callback(std::span<const u8>{read_buffer_.data(), got.bytes});
    const std::lock_guard lock{mutex_};
    if (!on_read_) on_read_ = std::move(callback);
}

void SocketStream::fire_close_once() {
    UniqueFunction<void()> callback;
    {
        const std::lock_guard lock{mutex_};
        if (close_fired_ || !on_close_) return;
        close_fired_ = true;
        callback = std::move(on_close_);
    }
    callback();
}

std::size_t SocketStream::send_locked(std::span<const u8> bytes) noexcept {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const IoResult result = send_some(socket_.get(), bytes.subspan(sent));
        if (result.error == EPIPE || result.error == ECONNRESET) {
            send_closed_ = true;
            outbound_.clear();
            outbound_sent_ = 0;
            return sent;
        }
        if (result.error != 0) {
            finish_locked();
            return sent;
        }
        if (result.would_block || result.bytes == 0) break;
        sent += result.bytes;
    }
    return sent;
}

void SocketStream::flush_locked() noexcept {
    const std::size_t sent = send_locked(std::span<const u8>{outbound_}.subspan(outbound_sent_));
    if (finished_ || send_closed_) return;
    outbound_sent_ += sent;
    if (outbound_sent_ == outbound_.size()) {
        outbound_.clear();
        outbound_sent_ = 0;
    } else if (outbound_sent_ >= outbound_.size() / 2) {
        outbound_.erase(outbound_.begin(), outbound_.begin() + static_cast<std::ptrdiff_t>(outbound_sent_));
        outbound_sent_ = 0;
    }
}

void SocketStream::finish_locked() noexcept {
    if (finished_) return;
    finished_ = true;
    outbound_.clear();
    outbound_sent_ = 0;
    ::shutdown(socket_.get(), SHUT_RDWR);
    wake_.wake();
}

}  // namespace reboot::posix
