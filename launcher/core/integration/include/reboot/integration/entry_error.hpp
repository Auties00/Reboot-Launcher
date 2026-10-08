#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/integration/integration_kind.hpp"

namespace reboot::integration {

enum class EntryErrorCode : u8 {
    NoItems,
    // Another app's handler; `owner` is what it runs.
    Foreign,
    Unsupported,
    InspectFailed,
    WriteFailed,
    RemoveFailed,
    // The written entry still is not registered with our arguments.
    NotApplied,
};

struct EntryError {
    EntryErrorCode code{};
    IntegrationKind kind{};
    std::string owner;
    // The registrar's own error.
    std::optional<Diagnostic> cause;
};

[[nodiscard]] Diagnostic to_diagnostic(const EntryError& error);

}  // namespace reboot::integration
