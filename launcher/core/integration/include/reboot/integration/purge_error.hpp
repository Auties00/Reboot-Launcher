#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/integration/purge_scope.hpp"

namespace rb::integration {

enum class PurgeErrorCode : u8 {
    // See PurgeBlockers.
    Blocked,
    // A root, or a folder holding the data root or the install; checked before anything is deleted.
    UnsafeTarget,
    // Reported once every other target was tried, with each failure as a cause.
    RemoveFailed,
};

struct PurgeError {
    PurgeErrorCode code{};
    PurgeScope scope{};
    u64 sessions = 0;
    u64 ops = 0;
    bool backend_running = false;
    std::optional<NativePath> path;
    std::vector<Diagnostic> causes;
};

[[nodiscard]] Diagnostic to_diagnostic(const PurgeError& error);

}  // namespace rb::integration
