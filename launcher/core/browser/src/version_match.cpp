#include "reboot/browser/version_match.hpp"

#include <array>
#include <optional>
#include <utility>

#include "reboot/foundation/diag.hpp"

namespace reboot::browser {

namespace {

constexpr std::array<std::pair<GameVersion, GameVersion>, 2> kAliases{{
    {GameVersion{5, 1, std::nullopt}, GameVersion{5, 0, 1}},
    {GameVersion{6, 2, std::nullopt}, GameVersion{6, 0, 2}},
}};

std::optional<GameVersion> alias_of(const GameVersion& version) {
    for (const auto& [short_form, long_form] : kAliases) {
        if (version == short_form) return long_form;
        if (version == long_form) return short_form;
    }
    return std::nullopt;
}

}  // namespace

bool same_game_version(std::string_view server_version, const GameVersion& wanted) {
    const auto parsed = GameVersion::parse(server_version);
    if (!parsed) return false;
    if (*parsed == wanted) return true;
    const auto alias = alias_of(wanted);
    return alias && *parsed == *alias;
}

std::vector<u32> buckets_for(const GameVersion& version) {
    std::vector<u32> buckets{version.bucket()};
    if (const auto alias = alias_of(version); alias && alias->bucket() != buckets.front())
        buckets.push_back(alias->bucket());
    return buckets;
}

}  // namespace reboot::browser
