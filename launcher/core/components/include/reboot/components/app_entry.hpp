#pragma once

#include <optional>
#include <string>

#include "reboot/components/manifest_platform.hpp"
#include "reboot/components/remote_file.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace rb::components {

enum class AppPackageKind : u8 { Velopack, Tarball };

// One app release for a platform and channel; `updates` applies it.
struct AppEntry {
    ManifestPlatform platform;
    std::string channel;
    SemVer version;
    // Below it new sessions are refused; running ones are left alone.
    std::optional<SemVer> min_supported;
    AppPackageKind kind{};
    RemoteFile package;
    // Rollback is a serial-bumped manifest that names an older version with this set.
    bool downgrade_ok = false;

    bool operator==(const AppEntry&) const = default;
};

// The version check gates only the offer; the manifest itself was admitted without it.
[[nodiscard]] inline bool offers_update(const AppEntry& entry, const SemVer& installed) {
    return entry.version > installed || (entry.downgrade_ok && entry.version != installed);
}

}  // namespace rb::components
