#pragma once

#include "reboot/components/component_ref.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::components {

enum class ComponentState : u8 { Missing, Fetching, Ready, Broken };

struct ComponentInfo {
    ComponentRef ref;
    ComponentState state{};
    u64 size_bytes = 0;
    u32 pins = 0;
    // The version the manifest selects for new sessions.
    bool selected = false;
    // Kept as N-1 until the selected version completes one good session.
    bool last_good = false;
};

}  // namespace rb::components
