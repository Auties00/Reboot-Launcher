#pragma once

#include <optional>

#include "reboot/foundation/version.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/support/host_inputs.hpp"
#include "reboot/support/support_role.hpp"

namespace rb::support {

// The engine resolves a library build or a version and changelist into these fields.
struct SupportQuery {
    // Absent while version detection has no confirmed answer.
    std::optional<GameVersion> version;
    std::optional<Changelist> cl;
    SupportRole role = SupportRole::Play;
    ports::RunnerKind runner = ports::RunnerKind::Native;
    // The above-cap opt-in applies only to builds already in the library.
    bool imported = false;
    bool above_cap_opt_in = false;
    // Play only.
    bool custom_auth_dll = false;
    // Play only: false for a Local or Remote backend target.
    bool embedded_backend = true;
    // Host, and play with the linked auto server: the binary that would host, as pinned by the
    // session. Absent until that binary has been described.
    std::optional<HostInputs> server;

    bool operator==(const SupportQuery&) const = default;
};

}  // namespace rb::support
