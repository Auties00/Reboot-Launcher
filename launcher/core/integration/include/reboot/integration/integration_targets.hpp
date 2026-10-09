#pragma once

#include <optional>

#include "reboot/foundation/native_path.hpp"
#include "reboot/integration/entry_flavor.hpp"
#include "reboot/integration/integration_kind.hpp"

namespace rb::integration {

// The programs in IPlatformPaths::exe_dir; registrars map them to stable entries, so core never compares paths.
struct IntegrationTargets {
    EntryFlavor flavor{};
    // Handles reboot:// links and owns the menu entry; nullopt where no GUI ships (Linux v1).
    std::optional<NativePath> gui_exe;
    NativePath engine_exe;
};

// What IIntegrationRegistrar::apply gets for `kind`; nullopt means Unsupported, without asking the registrar.
[[nodiscard]] std::optional<NativePath> entry_program(IntegrationKind kind, const IntegrationTargets& targets);

}  // namespace rb::integration
