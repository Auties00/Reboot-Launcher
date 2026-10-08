#pragma once

#include <string>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace reboot::identity {

// 10.0.9 sent the literal "Rebooted" for Placeholder; plan_login puts a session ticket there.
enum class LegacyPassword : u8 { UserPassword, Placeholder };

struct LegacyLogin {
    std::string auth_login;
    LegacyPassword password{};

    bool operator==(const LegacyLogin&) const = default;
};

// The 10.0.9 login chain (common/lib/src/util/game.dart createRebootArgs and _parseUsername),
// golden-tested against tests/data/legacy_derive.json. -AUTH_TYPE was always epic.
// - With a password: the username verbatim, even when empty.
// - Without: every byte outside [A-Za-z0-9] removed, "Player" when nothing is left, then
//   "@projectreboot.dev". 10.0.9 used "Player" for the host too; effective_login only passes
//   valid display names, so that fallback is never reached through it.
[[nodiscard]] LegacyLogin legacy_derive(std::string_view username, bool has_password);

}  // namespace reboot::identity
