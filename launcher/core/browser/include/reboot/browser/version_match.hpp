#pragma once

#include <string_view>
#include <vector>

#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace reboot::browser {

// Capabilities: game-builds.+2, server-browser.+28.
// Exact equality, except the two spellings 10.x shipped for the same builds: 5.01 = 5.0.1 and
// 6.02 = 6.0.2. A server version that does not parse matches nothing.
[[nodiscard]] bool same_game_version(std::string_view server_version, const GameVersion& wanted);

// The buckets a view needs to see every server of `version`: its own, plus the alias's when the
// alias lands in another bucket.
[[nodiscard]] std::vector<u32> buckets_for(const GameVersion& version);

}  // namespace reboot::browser
