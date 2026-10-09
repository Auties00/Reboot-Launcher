#include "macos_version.hpp"

#include <charconv>
#include <system_error>

namespace reboot::os_macos::platform {

std::optional<u32> macos_major_version(std::string_view product_version) {
    const std::string_view major = product_version.substr(0, product_version.find('.'));
    if (major.empty()) return std::nullopt;
    u32 value = 0;
    const auto [end, error] = std::from_chars(major.data(), major.data() + major.size(), value);
    if (error != std::errc{} || end != major.data() + major.size()) return std::nullopt;
    return value;
}

}  // namespace reboot::os_macos::platform
