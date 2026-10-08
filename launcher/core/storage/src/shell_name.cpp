#include "reboot/storage/shell_name.hpp"

#include <algorithm>

#include "messages.hpp"

namespace reboot::storage {

namespace {

constexpr std::size_t kMaxShellNameBytes = 32;

[[nodiscard]] constexpr bool is_shell_name_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
}

}  // namespace

Result<ShellName> ShellName::parse(std::string_view text) {
    if (text.empty() || text.size() > kMaxShellNameBytes || !std::ranges::all_of(text, is_shell_name_char))
        return invalid_input(msg::kInvalidShellName).arg("name", text).fail();
    return ShellName{std::string(text)};
}

}  // namespace reboot::storage
