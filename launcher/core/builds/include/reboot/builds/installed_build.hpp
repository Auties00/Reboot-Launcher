#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "reboot/builds/version_source.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace reboot::builds {

// Unchecked until this engine run resolved the layout once; Missing means it no longer resolves.
enum class BuildPresence : u8 { Unchecked, Present, Missing };

struct InstalledBuild {
    BuildId id;
    std::string name;
    // Host-native; the engine maps it into Wine at launch.
    NativePath root;
    std::optional<GameVersion> version;
    std::optional<Changelist> cl;
    std::optional<VersionSource> version_source;
    std::optional<CatalogEntryId> catalog_entry;
    std::chrono::system_clock::time_point added_at;
    // From another OS or a missing drive; kept and shown, never dropped.
    bool needs_relocation = false;
    BuildPresence presence = BuildPresence::Unchecked;

    // Gates (support, chapter one, S20) run only on a confirmed version.
    [[nodiscard]] bool version_confirmed() const noexcept {
        return version.has_value() && version_source.has_value();
    }

    bool operator==(const InstalledBuild&) const = default;
};

}  // namespace reboot::builds
