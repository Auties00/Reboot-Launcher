#pragma once

#include <array>
#include <span>
#include <vector>

#include "reboot/components/component_ref.hpp"
#include "reboot/components/manifest_platform.hpp"
#include "reboot/components/remote_file.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace reboot::components {

struct PayloadFile {
    PayloadRole role{};
    RemoteFile file;

    bool operator==(const PayloadFile&) const = default;
};

// One payload version, all Windows x64 PEs: at most one file per PayloadRole, ClientDll always.
struct PayloadEntry {
    SemVer version;
    u16 payload_abi = 0;
    std::vector<PayloadFile> files;

    bool operator==(const PayloadEntry&) const = default;

    [[nodiscard]] const PayloadFile* find(PayloadRole role) const noexcept {
        for (const PayloadFile& file : files)
            if (file.role == role) return &file;
        return nullptr;
    }
};

inline constexpr std::array<PayloadRole, 1> kNativePayloadRoles{PayloadRole::ClientDll};
inline constexpr std::array<PayloadRole, 2> kWinePayloadRoles{PayloadRole::ClientDll, PayloadRole::Winhost};

// What a play session on `os` loads: ClientDll everywhere, plus Winhost where play runs under Wine.
[[nodiscard]] constexpr std::span<const PayloadRole> required_payload_roles(ManifestOs os) noexcept {
    if (os == ManifestOs::Windows) return kNativePayloadRoles;
    return kWinePayloadRoles;
}

// Fails with components.payload_abi_mismatch unless payload_abi equals VersionStreams::payload_abi.
[[nodiscard]] Result<void> check_payload_abi(const PayloadEntry& entry);

}  // namespace reboot::components
