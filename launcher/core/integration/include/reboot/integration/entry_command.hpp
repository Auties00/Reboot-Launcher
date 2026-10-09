#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/integration/entry_flavor.hpp"
#include "reboot/integration/integration_kind.hpp"

namespace rb::integration {

inline constexpr std::string_view kActivateUrlFlag = "--activate-url";
inline constexpr std::string_view kRunVerb = "run";
inline constexpr std::string_view kServiceManagerOrigin = "--origin=service-manager";

// "%1" for Windows, "%u" for FreeDesktop; nullopt for Apple, whose handler gets links by Apple Event.
[[nodiscard]] std::optional<std::string_view> url_placeholder(EntryFlavor flavor) noexcept;

// What our scheme or Autostart command ends with; nullopt where no command is checked.
[[nodiscard]] std::optional<std::vector<std::string>> expected_args(IntegrationKind kind, EntryFlavor flavor);

// Program first. Double quotes group and \" is a literal quote; nullopt for an unbalanced quote or no token.
[[nodiscard]] std::optional<std::vector<std::string>> split_entry_command(std::string_view text);

}  // namespace rb::integration
