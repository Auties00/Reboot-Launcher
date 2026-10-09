#include "reboot/integration/openable_url.hpp"

#include <cstddef>

namespace rb::integration {

namespace {

constexpr std::string_view kHttpsPrefix = "https://";

[[nodiscard]] char ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

bool is_openable_url(std::string_view url) noexcept {
    if (url.size() <= kHttpsPrefix.size()) return false;
    for (std::size_t i = 0; i < kHttpsPrefix.size(); ++i)
        if (ascii_lower(url[i]) != kHttpsPrefix[i]) return false;
    for (const char c : url) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte <= 0x20 || byte == 0x7f) return false;
    }
    const std::string_view rest = url.substr(kHttpsPrefix.size());
    std::string_view authority = rest.substr(0, rest.find_first_of("/?#"));
    if (const std::size_t at = authority.rfind('@'); at != std::string_view::npos) authority.remove_prefix(at + 1);
    return !authority.empty() && authority.front() != ':';
}

}  // namespace rb::integration
