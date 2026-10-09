#pragma once

#include <string>
#include <string_view>

namespace reboot::identity {

// The -AUTH_LOGIN that third-party backends key an account by, golden-tested against
// tests/data/third_party_login.json: every byte outside [A-Za-z0-9] removed, "Player" when nothing
// is left, then "@projectreboot.dev". effective_login only passes valid display names, so the
// "Player" fallback is never reached through it.
[[nodiscard]] std::string third_party_login(std::string_view name);

}  // namespace reboot::identity
