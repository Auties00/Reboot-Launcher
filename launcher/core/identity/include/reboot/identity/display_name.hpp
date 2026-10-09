#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "reboot/contracts/backend.hpp"
#include "reboot/foundation/diag.hpp"

namespace rb {
class IRandom;
}

namespace rb::identity {

// [A-Za-z0-9] only, so the name round-trips exactly through the account id.
inline constexpr std::size_t kMinDisplayNameLength = 3;
inline constexpr std::size_t kMaxDisplayNameLength = 16;

// Fails with identity.display_name_too_short, identity.display_name_too_long or
// identity.display_name_invalid_character.
[[nodiscard]] Result<void> validate_display_name(std::string_view name);

// "Player" plus 6 digits for the client, "Host" plus 6 digits for the host. Cosmetic: the tag
// already makes the account id unique.
[[nodiscard]] std::string default_display_name(contracts::backend::AccountRole role, IRandom& random);

// True for a name of the default_display_name shape for `role`.
[[nodiscard]] bool is_default_display_name(std::string_view name, contracts::backend::AccountRole role) noexcept;

}  // namespace rb::identity
