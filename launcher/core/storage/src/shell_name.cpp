#include "reboot/storage/shell_name.hpp"

#include <algorithm>

#include "messages.hpp"

namespace reboot::storage {

namespace {

constexpr std::size_t kMaxShellNameBytes = 32;

[[nodiscard]] constexpr bool is_shell_name_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
}

// Windows opens a device, not config/frontend/<name>.json, for these names.
[[nodiscard]] bool is_windows_device_name(std::string_view text) noexcept {
    if (text == "con" || text == "prn" || text == "aux" || text == "nul") return true;
    return text.size() == 4 && (text.starts_with("com") || text.starts_with("lpt")) && text[3] >= '0' &&
           text[3] <= '9';
}

}  // namespace

Result<ShellName> ShellName::parse(std::string_view text) {
    if (text.empty() || text.size() > kMaxShellNameBytes || !std::ranges::all_of(text, is_shell_name_char) ||
        is_windows_device_name(text))
        return invalid_input(msg::kInvalidShellName).arg("name", text).fail();
    return ShellName{std::string(text)};
}

}  // namespace reboot::storage
