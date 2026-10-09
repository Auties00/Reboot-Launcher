#pragma once

#include <variant>

#include "reboot/builds/installed_build.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::builds {

// Extracted to `folder`, but detection or registration failed. Clients offer "Import anyway"
// (ImportService with the catalog entry) and "Delete files" (BuildInstaller::start_delete_unregistered).
struct UnregisteredInstall {
    NativePath folder;
    Diagnostic reason;
};

using InstallOutcome = std::variant<InstalledBuild, UnregisteredInstall>;

}  // namespace rb::builds
