#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "reboot/components/manifest_platform.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/support/cell_inputs.hpp"
#include "reboot/support/support_cell_key.hpp"

namespace reboot::support {

enum class EvidenceResult : u8 { Pass, Fail };

// One logged test-rig run. It counts for its cell only while `inputs` and `os` equal the
// current ones; `inputs` holds the alternative that matches `cell.role`.
struct EvidenceRecord {
    SupportCellKey cell;
    CellInputs inputs;
    components::ManifestOs os = components::ManifestOs::Windows;
    CatalogEntryId build;
    GameVersion version;
    std::optional<Changelist> cl;
    std::chrono::system_clock::time_point recorded_at;
    EvidenceResult result = EvidenceResult::Fail;
    std::string log_ref;

    bool operator==(const EvidenceRecord&) const = default;
};

}  // namespace reboot::support
