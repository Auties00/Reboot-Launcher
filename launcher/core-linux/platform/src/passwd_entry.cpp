#include "passwd_entry.hpp"

#include <cerrno>
#include <pwd.h>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace reboot::os_linux::platform {

namespace {

constexpr std::size_t kInitialBuffer = 16 * 1024;
constexpr std::size_t kMaxBuffer = 1024 * 1024;

}  // namespace

std::optional<PasswdEntry> read_passwd(u32 uid) {
    const long suggested = ::sysconf(_SC_GETPW_R_SIZE_MAX);
    std::vector<char> buffer(suggested > 0 ? static_cast<std::size_t>(suggested) : kInitialBuffer);
    for (;;) {
        passwd entry{};
        passwd* found = nullptr;
        const int error = ::getpwuid_r(static_cast<uid_t>(uid), &entry, buffer.data(), buffer.size(), &found);
        if (error == ERANGE && buffer.size() < kMaxBuffer) {
            buffer.resize(buffer.size() * 2);
            continue;
        }
        if (error == EINTR) continue;
        if (error != 0 || found == nullptr || found->pw_dir == nullptr) return std::nullopt;
        const std::string_view home{found->pw_dir};
        if (!home.starts_with('/')) return std::nullopt;
        return PasswdEntry{.name = found->pw_name != nullptr ? std::string(found->pw_name) : std::string(),
                           .home = NativePath{home}.lexically_normal()};
    }
}

}  // namespace reboot::os_linux::platform
