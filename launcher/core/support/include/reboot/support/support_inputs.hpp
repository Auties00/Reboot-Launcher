#pragma once

#include <vector>

#include "reboot/components/component_ref.hpp"
#include "reboot/components/manifest_platform.hpp"
#include "reboot/contracts/backend.hpp"
#include "reboot/support/evidence_record.hpp"
#include "reboot/support/runner_pin.hpp"

namespace reboot::support {

// What play cells are keyed to on this machine. The game server is not here: each host query
// carries the binary it would run.
struct SupportInputs {
    components::ManifestOs os = components::build_platform().os;
    // The pinned payload's client DLL, from the release manifest.
    components::Sha256Digest client_dll_sha256{};
    contracts::backend::ContentVersion backend_content;
    // The play runners this OS offers; a runner missing here is RunnerUnavailable.
    std::vector<RunnerPin> runners;
    // From parse_matrix_report.
    std::vector<EvidenceRecord> evidence;
};

}  // namespace reboot::support
