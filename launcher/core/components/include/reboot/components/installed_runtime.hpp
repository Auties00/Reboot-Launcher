#pragma once

#include <optional>

#include "reboot/base/native_path.hpp"
#include "reboot/components/component_ref.hpp"
#include "reboot/ports/session_host.hpp"

namespace rb::components {

// An unpacked runtime archive; compat turns `root` into a RuntimeLayout.
struct InstalledRuntime {
    ComponentRef ref;
    RuntimeKind kind{};
    NativePath root;
    // The manifest entry's boot_inject when the runtime was acquired.
    std::optional<ports::BootStrategy> boot_inject;
};

}  // namespace rb::components
