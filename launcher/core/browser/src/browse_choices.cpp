#include "reboot/browser/browse_choices.hpp"

#include <algorithm>

#include "reboot/browser/version_match.hpp"

namespace rb::browser {

ViewSpec make_view_spec(const BrowseChoices& choices, std::span<const GameVersion> installed, u32 window) {
    ViewSpec spec;
    if (choices.versions == VersionScope::Installed) {
        for (const GameVersion& version : installed)
            for (const u32 bucket : buckets_for(version)) spec.versions.buckets.push_back(bucket);
        std::ranges::sort(spec.versions.buckets);
        const auto duplicates = std::ranges::unique(spec.versions.buckets);
        spec.versions.buckets.erase(duplicates.begin(), duplicates.end());
    }
    spec.password = choices.password;
    spec.region = choices.region;
    spec.sort = choices.sort;
    spec.window = window;
    return spec;
}

}  // namespace rb::browser
