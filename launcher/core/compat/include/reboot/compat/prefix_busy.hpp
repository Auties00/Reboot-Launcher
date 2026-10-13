#pragma once

#include "reboot/base/diag.hpp"
#include "reboot/base/types.hpp"
#include "reboot/compat/runner_profile.hpp"

namespace rb::compat {

// A change to the runner's prefix was due while another session uses it.
struct PrefixBusy {
    RunnerKind runner{};
    SessionId holder;

    bool operator==(const PrefixBusy&) const = default;
};

// compat.prefix_busy.
[[nodiscard]] Diagnostic to_diagnostic(const PrefixBusy& error);

}  // namespace rb::compat
