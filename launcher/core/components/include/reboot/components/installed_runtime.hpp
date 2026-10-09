#pragma once

#include "reboot/components/component_ref.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::components {

// An unpacked runtime archive; compat turns `root` into a RuntimeLayout.
struct InstalledRuntime {
    ComponentRef ref;
    RuntimeKind kind{};
    NativePath root;
};

}  // namespace rb::components
