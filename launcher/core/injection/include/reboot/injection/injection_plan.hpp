#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "reboot/injection/client_features.hpp"
#include "reboot/injection/dll_slot.hpp"
#include "reboot/injection/injection_inputs.hpp"
#include "reboot/injection/net_mode.hpp"
#include "reboot/injection/planned_dll.hpp"
#include "reboot/ports/session_host.hpp"

namespace rb::injection {

struct InjectionPlan {
    NetMode net_mode = NetMode::Isolated;
    ClientFeatures features;
    // In load order and slot-labelled, so crash and support reports can list them. Every entry is
    // Early and shares one strategy, so a custom auth DLL always loads after ours.
    std::vector<PlannedDll> dlls;

    [[nodiscard]] std::vector<ports::InjectEntry> inject_entries() const;
    // The slot a ports::Injected event is about; nullopt for a path this plan never injects.
    [[nodiscard]] std::optional<DllSlot> slot_of(const NativePath& path) const;
};

// Capabilities: dll-injection.dll-set-resolution, dll-injection.timing, dll-injection.+6.
// Our client DLL is always injected; a custom auth DLL follows it, switches to LegacyFixed and
// turns auth_redirect off. Both use resolve_boot_strategy(), so no runner gate is bypassed.
[[nodiscard]] InjectionPlan plan_injection(const InjectionInputs& inputs);

}  // namespace rb::injection
