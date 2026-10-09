#include "file_uri.hpp"

#include "reboot/foundation/text.hpp"

namespace rb::os_linux::platform {

std::string file_uri(const NativePath& path) {
    constexpr std::string_view kHex = "0123456789ABCDEF";
    std::string uri = "file://";
    for (const char c : path.string()) {
        const auto byte = static_cast<unsigned char>(c);
        const bool plain = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
                           byte == '-' || byte == '.' || byte == '_' || byte == '~' || byte == '/';
        if (plain) {
            uri += c;
        } else {
            uri += '%';
            uri += kHex[byte >> 4];
            uri += kHex[byte & 0x0F];
        }
    }
    return uri;
}

bool is_https_url(std::string_view url) noexcept {
    constexpr std::string_view kScheme = "https://";
    if (url.size() <= kScheme.size() || !iequals_ascii(url.substr(0, kScheme.size()), kScheme)) return false;
    const char first = url[kScheme.size()];
    return first != '/' && first != '?' && first != '#';
}

}  // namespace rb::os_linux::platform
