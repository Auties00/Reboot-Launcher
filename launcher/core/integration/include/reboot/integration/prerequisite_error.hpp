#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/integration/prerequisite_id.hpp"

namespace reboot::integration {

enum class PrerequisiteErrorCode : u8 {
    UnknownId,
    NotRemediable,
    // The probe does not report it on this OS.
    NotApplicable,
    RemediationFailed,
    // The remedy ran but the re-check still reports it missing.
    StillMissing,
};

struct PrerequisiteError {
    PrerequisiteErrorCode code{};
    // Set for UnknownId, where `id` is meaningless.
    std::string text;
    PrerequisiteId id{};
    std::optional<Diagnostic> cause;
};

[[nodiscard]] Diagnostic to_diagnostic(const PrerequisiteError& error);

}  // namespace reboot::integration
