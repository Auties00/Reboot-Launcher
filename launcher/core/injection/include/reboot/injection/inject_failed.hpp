#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/injection/dll_slot.hpp"

namespace reboot::injection {

// Capabilities: dll-injection.timing.
// A planned DLL that did not load, from a failed ports::Injected and InjectionPlan::slot_of; it
// ends the session.
struct InjectFailed {
    DllSlot slot{};
    NativePath path;
    // As ports::Injected reports it: a GuestWindows code under Wine.
    std::optional<SystemError> os;

    bool operator==(const InjectFailed&) const = default;
};

// injection.inject_failed with the path, the slot and the OS code.
[[nodiscard]] Diagnostic to_diagnostic(const InjectFailed& error);

}  // namespace reboot::injection
