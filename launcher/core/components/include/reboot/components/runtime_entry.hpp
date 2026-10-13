#pragma once

#include <optional>
#include <string>

#include "reboot/components/component_ref.hpp"
#include "reboot/components/manifest_platform.hpp"
#include "reboot/components/remote_file.hpp"
#include "reboot/ports/session_host.hpp"

namespace rb::components {

// A play runtime archive, unpacked into its own directory. `id` is the component id and is
// unique across the manifest; a newer build of a runtime gets a new id.
// - `platform` is nullopt only for VcRedist, one Windows PE that serves the Wine prefixes of every
//   macOS and Linux platform; its archive holds vc_redist.x64.exe at its root.
// - `boot_inject` is set once the early-bird spike passed on this runtime (injection's
//   RuntimeBootDefault::proven); absent, or a name this build does not know, forces AfterResume.
struct RuntimeEntry {
    std::string id;
    RuntimeKind kind{};
    std::optional<ManifestPlatform> platform;
    std::string version;
    RemoteFile archive;
    std::optional<ports::BootStrategy> boot_inject;

    bool operator==(const RuntimeEntry&) const = default;
};

// Whether a launcher built for `target` may use `entry`; VcRedist matches every OS but Windows.
[[nodiscard]] constexpr bool runtime_matches(const RuntimeEntry& entry, ManifestPlatform target) noexcept {
    if (entry.platform) return *entry.platform == target;
    return target.os != ManifestOs::Windows;
}

}  // namespace rb::components
