#include "reboot/updates/update_offer.hpp"

#include "reboot/components/release_manifest.hpp"

namespace reboot::updates {

std::string_view manifest_channel(storage::UpdateChannel channel) noexcept {
    switch (channel) {
        case storage::UpdateChannel::Stable: return "stable";
        case storage::UpdateChannel::Beta: return "beta";
    }
    return "stable";
}

std::optional<UpdateOffer> select_offer(const components::ReleaseManifest& manifest,
                                        components::ManifestPlatform platform, storage::UpdateChannel channel,
                                        const SemVer& installed) {
    const std::string_view own = manifest_channel(channel);
    const std::string_view stable = manifest_channel(storage::UpdateChannel::Stable);
    const components::AppEntry* best = nullptr;
    for (const components::AppEntry& entry : manifest.apps) {
        if (entry.platform != platform) continue;
        bool candidate = false;
        if (entry.channel == own) candidate = components::offers_update(entry, installed);
        // Only a newer stable counts on Beta: a stable rollback must not pull beta users back.
        else if (channel == storage::UpdateChannel::Beta && entry.channel == stable)
            candidate = entry.version > installed;
        if (candidate && (best == nullptr || best->version < entry.version)) best = &entry;
    }
    if (best == nullptr) return std::nullopt;
    return UpdateOffer{*best, best->min_supported && installed < *best->min_supported};
}

}  // namespace reboot::updates
