#pragma once

#include <variant>

#include "reboot/base/diag.hpp"
#include "reboot/base/native_path.hpp"
#include "reboot/builds/installed_build.hpp"

namespace rb::builds {

// Extracted to `folder`, but detection or registration failed. Clients offer "Import anyway"
// (ImportService with the catalog entry) and "Delete files" (BuildInstaller::start_delete_unregistered).
struct UnregisteredInstall {
    NativePath folder;
    Diagnostic reason;
};

using InstallOutcome = std::variant<InstalledBuild, UnregisteredInstall>;

}  // namespace rb::builds
