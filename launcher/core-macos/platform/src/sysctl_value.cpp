#include "darwin.hpp"

#include "sysctl_value.hpp"

#include <sys/sysctl.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace reboot::os_macos::platform {

std::optional<std::string> sysctl_string(const char* name) {
    std::size_t size = 0;
    if (::sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) return std::nullopt;
    std::string value(size, '\0');
    if (::sysctlbyname(name, value.data(), &size, nullptr, 0) != 0) return std::nullopt;
    value.resize(::strnlen(value.data(), size));
    return value;
}

std::optional<i64> sysctl_integer(const char* name) {
    std::int64_t wide = 0;
    std::size_t size = sizeof wide;
    if (::sysctlbyname(name, &wide, &size, nullptr, 0) != 0) return std::nullopt;
    if (size == sizeof(std::int32_t)) {
        std::int32_t narrow = 0;
        std::memcpy(&narrow, &wide, sizeof narrow);
        return narrow;
    }
    if (size == sizeof wide) return wide;
    return std::nullopt;
}

bool apple_silicon() { return sysctl_integer("hw.optional.arm64").value_or(0) == 1; }

}  // namespace reboot::os_macos::platform
