#pragma once

#include <compare>
#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"

namespace rb::storage {

// A frontend: "winui", "swiftui", "cli". Names config/frontend/<name>.json and per-shell state.
struct ShellName {
    std::string value;

    auto operator<=>(const ShellName&) const = default;

    // [a-z0-9-], 1 to 32 bytes, not a Windows device name; fails with storage.invalid_shell_name.
    [[nodiscard]] static Result<ShellName> parse(std::string_view text);
};

}  // namespace rb::storage
