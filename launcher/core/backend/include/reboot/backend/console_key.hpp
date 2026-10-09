#pragma once

#include <optional>
#include <string_view>

#include "reboot/backend/hid_usage.hpp"
#include "reboot/storage/console_key.hpp"

namespace rb::backend {

// Capabilities: auth-backend.console-key.
// The UE key name sent per session in ConfigureSession, which the backend binds to the game console;
// storage owns the type and its allowlist.
using ConsoleKey = storage::ConsoleKey;

// Nothing for a key a console binding cannot use. Keypad Enter is Enter and the ISO key next to
// left Shift is Backslash, since UE names neither apart.
[[nodiscard]] std::optional<ConsoleKey> console_key_from_hid(HidUsage usage);

// The key cap a picker shows, such as "F8", "`" or "Num 1"; empty for a name outside the allowlist.
[[nodiscard]] std::string_view key_label(const ConsoleKey& key) noexcept;

}  // namespace rb::backend
