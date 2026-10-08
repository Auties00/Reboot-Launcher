#pragma once

#include <vector>

#include "reboot/components/component_ref.hpp"
#include "reboot/contracts/game_server.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/support/cell_inputs.hpp"
#include "reboot/support/version_range.hpp"

namespace reboot::support {

// One described game-server binary; the ranges come from its GameServerDescription.
struct HostInputs {
    HostCellInputs pin;
    std::vector<VersionRange> ranges;

    bool operator==(const HostInputs&) const = default;
};

// Fails with support.malformed_server_description when a version_min or version_max is not a
// strict GameVersion, or a version or changelist range is inverted.
[[nodiscard]] Result<HostInputs> host_inputs_from(const components::Sha256Digest& game_server_sha256,
                                                  const contracts::game_server::GameServerDescription& description);

}  // namespace reboot::support
