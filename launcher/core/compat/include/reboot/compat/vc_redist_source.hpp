#pragma once

#include <string_view>

#include "reboot/components/pinned_runtime.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"

namespace rb::compat {

// At the root of the manifest's RuntimeKind::VcRedist runtime.
inline constexpr std::string_view kVcRedistInstaller = "vc_redist.x64.exe";

// Fetches and pins the VC++ redistributable; called only when seeding is needed, and the pin is
// held until the installer exits.
using VcRedistSource = UniqueFunction<void(UniqueFunction<void(Result<components::PinnedRuntime>)> done)>;

}  // namespace rb::compat
