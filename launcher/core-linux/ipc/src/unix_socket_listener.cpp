#include "reboot/os_linux/ipc/unix_socket_listener.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

#include "engine_socket_path.hpp"
#include "linux_peer_credentials.hpp"
#include "messages.hpp"
#include "runtime_base_check.hpp"
#include "socket_activation.hpp"

namespace reboot::os_linux::ipc {
namespace {

[[nodiscard]] std::optional<std::string_view> environment_value(const char* name) {
    const char* const value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    return std::string_view{value};
}

[[nodiscard]] std::unexpected<Diagnostic> invalid_socket(std::string_view count) {
    return make_diag(ErrorDomain::Platform, kInheritedSocketInvalid).arg("count", count).fail();
}

[[nodiscard]] bool socket_option_is(int fd, int option, int expected) noexcept {
    int value = 0;
    socklen_t length = sizeof value;
    return ::getsockopt(fd, SOL_SOCKET, option, &value, &length) == 0 && value == expected;
}

[[nodiscard]] bool is_listening_unix_stream(int fd) noexcept {
    struct stat info {};
    if (::fstat(fd, &info) != 0 || !S_ISSOCK(info.st_mode)) return false;
    return socket_option_is(fd, SO_DOMAIN, AF_UNIX) && socket_option_is(fd, SO_TYPE, SOCK_STREAM) &&
           socket_option_is(fd, SO_ACCEPTCONN, 1);
}

// The path `fd` is bound to; an abstract name reads as "@name" and an unnamed socket as "".
[[nodiscard]] std::optional<std::string> bound_path(int fd) {
    sockaddr_un address{};
    socklen_t length = sizeof address;
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) return std::nullopt;
    constexpr std::size_t kPathOffset = offsetof(sockaddr_un, sun_path);
    if (length <= kPathOffset) return std::string{};
    const std::size_t size = std::min<std::size_t>(length - kPathOffset, sizeof address.sun_path);
    if (address.sun_path[0] == '\0') return "@" + std::string(address.sun_path + 1, size - 1);
    return std::string(address.sun_path, ::strnlen(address.sun_path, size));
}

}  // namespace

UnixSocketListener::UnixSocketListener(NativePath runtime_base)
    : UnixSocketListenerBase(posix::PeerCredentialCheck{[](int fd) { return read_linux_peer(fd); },
                                                        static_cast<u32>(::geteuid())}),
      runtime_base_(std::move(runtime_base)) {}

Result<void> UnixSocketListener::listen(std::string_view endpoint_name,
                                        UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> on_accept) {
    if (auto socket = check_engine_socket_path(runtime_base_, endpoint_name); !socket)
        return std::unexpected(std::move(socket.error()));
    if (auto base = ensure_runtime_base(runtime_base_, static_cast<u32>(::geteuid())); !base) return base;
    return UnixSocketListenerBase::listen(endpoint_name, std::move(on_accept));
}

Result<std::optional<posix::UniqueFd>> UnixSocketListener::take_inherited_socket(const NativePath& socket_path) {
    const std::optional<std::string_view> listen_fds = environment_value("LISTEN_FDS");
    const SocketActivation activation =
        read_socket_activation({environment_value("LISTEN_PID"), listen_fds}, static_cast<u32>(::getpid()));
    if (!activation.for_us) return std::optional<posix::UniqueFd>{};
    if (activation.count != 1U) return invalid_socket(listen_fds.value_or("0"));

    constexpr int fd = kListenFdsStart;
    const int fd_flags = ::fcntl(fd, F_GETFD);
    if (fd_flags < 0) return invalid_socket("1");
    // Before the checks, so no child of ours holds the endpoint even when it is refused.
    if (::fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC) != 0 || !is_listening_unix_stream(fd)) return invalid_socket("1");

    const std::optional<std::string> inherited_path = bound_path(fd);
    if (!inherited_path) return invalid_socket("1");
    if (*inherited_path != socket_path.native()) {
        return make_diag(ErrorDomain::Platform, kInheritedSocketMismatch)
            .arg("inherited_path", *inherited_path)
            .arg("path", socket_path)
            .fail();
    }
    return std::optional<posix::UniqueFd>{posix::UniqueFd{fd}};
}

}  // namespace reboot::os_linux::ipc
