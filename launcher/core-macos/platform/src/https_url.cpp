#include "https_url.hpp"

#include "reboot/foundation/text.hpp"

namespace rb::os_macos::platform {

bool is_https_url(std::string_view url) noexcept {
    constexpr std::string_view kScheme = "https://";
    if (url.size() <= kScheme.size() || !iequals_ascii(url.substr(0, kScheme.size()), kScheme)) return false;
    for (const char c : url)
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F) return false;
    const char first = url[kScheme.size()];
    return first != '/' && first != '?' && first != '#' && first != '@' && first != ':';
}

}  // namespace rb::os_macos::platform
