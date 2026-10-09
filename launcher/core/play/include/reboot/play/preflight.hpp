#pragma once

#include <optional>
#include <vector>

#include "reboot/backend/backend_lease.hpp"
#include "reboot/backend/backend_upstream.hpp"
#include "reboot/builds/build_layout.hpp"
#include "reboot/builds/installed_build.hpp"
#include "reboot/catalog/build_flags.hpp"
#include "reboot/compat/prepared_prefix.hpp"
#include "reboot/compat/prepared_runtime.hpp"
#include "reboot/components/integrity_hold.hpp"
#include "reboot/components/pinned_payload.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/identity/account_record.hpp"
#include "reboot/identity/login_plan.hpp"
#include "reboot/injection/injection_plan.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/storage/settings_snapshot.hpp"
#include "reboot/support/support_verdict.hpp"

namespace rb::play {

// What a Wine runner session pins; absent on the Native runner.
struct WinePins {
    compat::PreparedRuntime runtime;
    compat::PreparedPrefix prefix;
};

// Covers game-launch.orchestration.
// Everything a play session pins before anything is spawned, so a later settings edit, catalog
// refresh or component update never reaches it. Move-only; the session's driver drops it at the end.
struct Preflight {
    // Its version is confirmed: start refuses a build without one.
    builds::InstalledBuild build;
    builds::BuildLayout layout;
    catalog::BuildFlags flags;
    support::SupportVerdict support;
    // The user accepted ConfirmUntested; the linked auto-server's HostStartRequest gets it too.
    bool untested_confirmed = false;
    storage::SettingsSnapshot settings;
    ports::RunnerKind runner = ports::RunnerKind::Native;
    // Scales GameControlHello and the other session deadlines.
    RunnerMultiplier multiplier = RunnerMultiplier::Native;
    std::optional<WinePins> wine;
    components::PinnedPayload payload;
    // Released once the session reports the injected DLLs loaded.
    std::vector<components::IntegrityHold> holds;
    identity::AccountRecord account;
    identity::LoginPlan login;
    backend::BackendLease lease;
    backend::BackendUpstream upstream;
    injection::InjectionPlan injection;
};

}  // namespace rb::play
