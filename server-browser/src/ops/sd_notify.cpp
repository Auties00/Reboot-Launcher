#include "ops/sd_notify.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace sb::ops {

void sd_notify(std::string_view state) noexcept {
    const char* path = std::getenv("NOTIFY_SOCKET");
    if (!path || !*path) return;
    sockaddr_un sa{};
    sa.sun_family = AF_UNIX;
    const std::size_t len = std::strlen(path);
    if (len >= sizeof(sa.sun_path)) return;
    std::memcpy(sa.sun_path, path, len);
    if (sa.sun_path[0] == '@') sa.sun_path[0] = '\0';  // abstract namespace
    const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    (void)::sendto(fd, state.data(), state.size(), MSG_NOSIGNAL, reinterpret_cast<sockaddr*>(&sa),
                   static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + len));
    ::close(fd);
}

}  // namespace sb::ops
