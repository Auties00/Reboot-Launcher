#pragma once

#include <optional>

#include "reboot/foundation/version.hpp"
#include "reboot/injection/pinned_dll.hpp"
#include "reboot/injection/runtime_boot_default.hpp"
#include "reboot/ports/session_host.hpp"

namespace reboot::injection {

// From the session's pinned inputs, so a later settings edit never changes a running session.
struct InjectionInputs {
    GameVersion version;
    // The catalog's boot_inject for this build on the session's runner.
    std::optional<ports::BootStrategy> build_boot;
    // kNativeBootDefault on Native, else the pinned runtime's manifest value.
    RuntimeBootDefault runtime_boot;
    // rb_client.dll as the pinned payload stores it.
    PinnedDll client_runtime;
    std::optional<PinnedDll> custom_auth;
};

}  // namespace reboot::injection
