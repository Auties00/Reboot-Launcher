#pragma once

#include <optional>
#include <string_view>

#include "reboot/components/app_entry.hpp"
#include "reboot/components/manifest_platform.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/storage/settings_values.hpp"

namespace rb::components {
struct ReleaseManifest;
}

namespace rb::updates {

struct UpdateOffer {
    components::AppEntry entry;
    // The installed build is below the entry's min_supported, so new sessions are refused.
    bool required = false;

    bool operator==(const UpdateOffer&) const = default;
};

[[nodiscard]] std::string_view manifest_channel(storage::UpdateChannel channel) noexcept;

// Beta also weighs the stable entry, so a newer stable release still reaches beta users.
[[nodiscard]] std::optional<UpdateOffer> select_offer(const components::ReleaseManifest& manifest,
                                                      components::ManifestPlatform platform,
                                                      storage::UpdateChannel channel, const SemVer& installed);

}  // namespace rb::updates
