#pragma once

#include <span>
#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"

namespace reboot::storage {

// An Unreal Engine key name ("F8", "Tilde"), the form the client DLL takes; UIs map key codes to it.
struct ConsoleKey {
    std::string name = "F8";

    bool operator==(const ConsoleKey&) const = default;

    // Exact, case-sensitive match against unreal_key_names(); fails with storage.invalid_console_key.
    [[nodiscard]] static Result<ConsoleKey> parse(std::string_view name);
};

// For pickers and the CLI.
[[nodiscard]] std::span<const std::string_view> unreal_key_names() noexcept;

}  // namespace reboot::storage
