#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::builds {

enum class PeErrorCode : u8 {
    NotPe,
    // An offset, size or count points outside the file or its section.
    Malformed,
    NoVersionResource,
    ResourceTooLarge,
    ReadFailed,
    Cancelled,
};

// Package-internal; PeVersionReader returns it as a Diagnostic, which detect_version wraps with the file.
struct PeError {
    PeErrorCode code = PeErrorCode::Malformed;
    // File offset of the structure that failed the check.
    u64 offset = 0;
    std::optional<Diagnostic> cause;
};

[[nodiscard]] Diagnostic to_diagnostic(const PeError& error);

}  // namespace reboot::builds
