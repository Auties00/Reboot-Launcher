#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/storage/load_report.hpp"

namespace rb::storage {

// A failed write makes a document InMemory, the next good one ReadWrite. The engine publishes it
// as EventKind::StorageModeChanged, coalesced per document.
struct StorageModeChanged {
    std::string document;
    StorageMode mode = StorageMode::ReadWrite;
    // The write error while InMemory.
    std::optional<Diagnostic> reason;
};

}  // namespace rb::storage
