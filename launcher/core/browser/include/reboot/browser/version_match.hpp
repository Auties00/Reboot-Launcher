#pragma once

#include <string_view>
#include <vector>

#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace rb::browser {

// Capabilities: game-builds.+2, server-browser.+28.
// Exact equality of the parsed versions: the catalog and the CL table name each build with one
// spelling, so no alias is needed. A server version that does not parse matches nothing.
[[nodiscard]] bool same_game_version(std::string_view server_version, const GameVersion& wanted);

// The buckets a view needs to see every server of `version`.
[[nodiscard]] std::vector<u32> buckets_for(const GameVersion& version);

}  // namespace rb::browser
