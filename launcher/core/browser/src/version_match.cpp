#include "reboot/browser/version_match.hpp"

#include "reboot/foundation/diag.hpp"

namespace rb::browser {

bool same_game_version(std::string_view server_version, const GameVersion& wanted) {
    const auto parsed = GameVersion::parse(server_version);
    return parsed && *parsed == wanted;
}

std::vector<u32> buckets_for(const GameVersion& version) { return {version.bucket()}; }

}  // namespace rb::browser
