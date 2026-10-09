#include "https_url.hpp"

#include <algorithm>

#include "reboot/foundation/text.hpp"

namespace rb::os_windows::platform {

bool is_https_url(std::string_view url) noexcept {
    constexpr std::string_view kScheme = "https://";
    if (url.size() <= kScheme.size() || !iequals_ascii(url.substr(0, kScheme.size()), kScheme)) return false;
    if (std::ranges::any_of(url, [](char c) { return static_cast<unsigned char>(c) <= 0x20 || c == 0x7F; })) return false;
    const std::string_view rest = url.substr(kScheme.size());
    const std::string_view authority = rest.substr(0, rest.find_first_of("/?#"));
    const std::string_view host = authority.substr(authority.rfind('@') == std::string_view::npos ? 0 : authority.rfind('@') + 1);
    return !host.empty() && host.front() != ':';
}

}  // namespace rb::os_windows::platform
