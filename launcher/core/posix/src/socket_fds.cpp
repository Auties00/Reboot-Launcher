#include "socket_fds.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <utility>

#include "reboot/posix/posix_error.hpp"
#include "unistd.hpp"

namespace reboot::posix {

namespace {

#if defined(__linux__)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

[[nodiscard]] Result<void> suppress_sigpipe([[maybe_unused]] int fd) {
#if defined(__APPLE__)
    const int on = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on) != 0)
        return std::unexpected(call_failed("setsockopt", errno));
#endif
    return {};
}

[[nodiscard]] bool would_block(int error) noexcept { return error == EAGAIN || error == EWOULDBLOCK; }

[[nodiscard]] sockaddr_un unix_address(const NativePath& path) noexcept {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.native().size() + 1);
    return address;
}

}  // namespace

Result<void> make_cloexec_nonblocking(int fd) {
    const int fd_flags = ::fcntl(fd, F_GETFD);
    if (fd_flags < 0 || ::fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC) != 0)
        return std::unexpected(call_failed("fcntl", errno));
    const int status_flags = ::fcntl(fd, F_GETFL);
    if (status_flags < 0 || ::fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) != 0)
        return std::unexpected(call_failed("fcntl", errno));
    return {};
}

Result<UniqueFd> make_unix_stream_socket() {
#if defined(__linux__)
    UniqueFd fd{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
    if (!fd.valid()) return std::unexpected(call_failed("socket", errno));
#else
    UniqueFd fd{::socket(AF_UNIX, SOCK_STREAM, 0)};
    if (!fd.valid()) return std::unexpected(call_failed("socket", errno));
    if (auto ready = make_cloexec_nonblocking(fd.get()); !ready) return std::unexpected(std::move(ready.error()));
#endif
    if (auto quiet = suppress_sigpipe(fd.get()); !quiet) return std::unexpected(std::move(quiet.error()));
    return fd;
}

Result<UniqueFd> accept_unix_stream(int listen_fd) {
    for (;;) {
#if defined(__linux__)
        UniqueFd fd{::accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK)};
#else
        UniqueFd fd{::accept(listen_fd, nullptr, nullptr)};
#endif
        if (!fd.valid()) {
            const int error = errno;
            if (error == EINTR) continue;
            // The peer gave up between poll and accept.
            if (would_block(error) || error == ECONNABORTED) return UniqueFd{};
            return std::unexpected(call_failed("accept", error));
        }
#if !defined(__linux__)
        if (auto ready = make_cloexec_nonblocking(fd.get()); !ready) return std::unexpected(std::move(ready.error()));
#endif
        if (auto quiet = suppress_sigpipe(fd.get()); !quiet) return std::unexpected(std::move(quiet.error()));
        return fd;
    }
}

int bind_unix(int socket_fd, const NativePath& path) noexcept {
    const sockaddr_un address = unix_address(path);
    return ::bind(socket_fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0 ? 0 : errno;
}

int connect_unix(int socket_fd, const NativePath& path) noexcept {
    const sockaddr_un address = unix_address(path);
    return ::connect(socket_fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0 ? 0 : errno;
}

IoResult send_some(int socket_fd, std::span<const u8> bytes) noexcept {
    for (;;) {
        const auto sent = ::send(socket_fd, bytes.data(), bytes.size(), kSendFlags);
        if (sent >= 0) return {.bytes = static_cast<std::size_t>(sent)};
        if (errno == EINTR) continue;
        if (would_block(errno)) return {.would_block = true};
        return {.error = errno};
    }
}

IoResult receive_some(int socket_fd, std::span<u8> out) noexcept {
    for (;;) {
        const auto received = ::recv(socket_fd, out.data(), out.size(), 0);
        if (received >= 0) return {.bytes = static_cast<std::size_t>(received)};
        if (errno == EINTR) continue;
        if (would_block(errno)) return {.would_block = true};
        return {.error = errno};
    }
}

WakePipe::WakePipe(UniqueFd read, UniqueFd write) noexcept : read_(std::move(read)), write_(std::move(write)) {}

Result<WakePipe> WakePipe::create() {
    std::array<int, 2> ends{-1, -1};
#if defined(__linux__)
    if (::pipe2(ends.data(), O_CLOEXEC | O_NONBLOCK) != 0) return std::unexpected(call_failed("pipe2", errno));
    return WakePipe{UniqueFd{ends[0]}, UniqueFd{ends[1]}};
#else
    if (::pipe(ends.data()) != 0) return std::unexpected(call_failed("pipe", errno));
    WakePipe wake{UniqueFd{ends[0]}, UniqueFd{ends[1]}};
    for (const int end : ends)
        if (auto ready = make_cloexec_nonblocking(end); !ready) return std::unexpected(std::move(ready.error()));
    return wake;
#endif
}

void WakePipe::wake() const noexcept {
    const u8 byte = 1;
    // A full pipe already guarantees a wake-up, so EAGAIN is ignored.
    while (::write(write_.get(), &byte, 1) < 0 && errno == EINTR) {
    }
}

void WakePipe::drain() const noexcept {
    std::array<u8, 64> sink{};
    for (;;) {
        const auto got = ::read(read_.get(), sink.data(), sink.size());
        if (got > 0) continue;
        if (got < 0 && errno == EINTR) continue;
        return;
    }
}

}  // namespace reboot::posix
