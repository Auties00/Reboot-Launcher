#pragma once

#include "reboot/components/component_ref.hpp"
#include "reboot/foundation/native_path.hpp"

namespace reboot::components {

// An unpacked runtime archive; compat turns `root` into a RuntimeLayout.
struct InstalledRuntime {
    ComponentRef ref;
    RuntimeKind kind{};
    NativePath root;
};

}  // namespace reboot::components
