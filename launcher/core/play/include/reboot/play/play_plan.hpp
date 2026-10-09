#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/identity/login_plan.hpp"
#include "reboot/injection/net_mode.hpp"
#include "reboot/play/required_decision.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/support/support_verdict.hpp"

namespace rb::play {

// Covers game-launch.orchestration.
// What start would do now, from state the strand holds; layout, payload, runtime and backend are checked by start.
struct PlayPlan {
    BuildId build;
    std::optional<GameVersion> version;
    // With the linked auto-server, AutoServerVerdict::tier: never above the server's host cell.
    support::SupportVerdict support;
    injection::NetMode net_mode = injection::NetMode::Isolated;
    ports::RunnerKind runner = ports::RunnerKind::Native;
    // Absent when the login cannot be planned; the reason is in `blockers`. Its auth_login is the
    // label UIs show.
    std::optional<identity::LoginPlan> login;
    // Each makes start fail: a Blocked verdict, a wrong session, a live play session, an
    // unplannable login, unparseable custom arguments.
    std::vector<Diagnostic> blockers;
    // Shown, never fatal: identity.password_in_argv, an Untested verdict's reasons.
    std::vector<Diagnostic> warnings;
    std::vector<RequiredDecision> decisions;

    [[nodiscard]] bool startable() const noexcept { return blockers.empty(); }
};

}  // namespace rb::play
