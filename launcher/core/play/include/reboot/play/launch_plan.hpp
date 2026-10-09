#pragma once

#include <string_view>
#include <vector>

#include "reboot/builds/build_layout.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/play/launch_args.hpp"
#include "reboot/ports/session_host.hpp"
#include "reboot/process/built_env.hpp"

namespace rb::play {

// The REBOOT_ROLE value of a play session.
inline constexpr std::string_view kClientRole = "client";

// Covers game-launch.orchestration, game-launch.arguments.
// The one launch of a play session; nothing in the build directory is created or modified for it
// beyond the `park` renames, which the session host undoes when the session ends.
struct LaunchPlan {
    // Host-native; WineSessionHost maps it into the prefix.
    NativePath exe;
    // The shipping exe's directory.
    NativePath cwd;
    LaunchArgs args;
    // Native: every EnvBuilder layer. Wine: only the channel layer, since WineSessionSetup::env
    // carries the others and winhost lays this over them; either way one REBOOT_CTL_TOKEN, our DLL's.
    process::BuiltEnv env;
    std::vector<ports::CompanionSpec> companions;
    // InjectionPlan::inject_entries(), in load order.
    std::vector<ports::InjectEntry> inject;
    // parked_for(layout), host-native.
    std::vector<NativePath> park;
    RunnerMultiplier multiplier = RunnerMultiplier::Native;

    // A copy for ISessionHost::launch; the caller wipes its argv and env once launch returns.
    [[nodiscard]] ports::SessionLaunch session_launch(SessionId session) const;
};

// FortniteLauncher.exe, then the EAC exe, each when the layout has it, without arguments; the
// session host spawns them suspended and never resumes them.
[[nodiscard]] std::vector<ports::CompanionSpec> companions_for(const builds::BuildLayout& layout);

// The build files a play session moves aside on every runner: each Aftermath DLL, absolute.
[[nodiscard]] std::vector<NativePath> parked_for(const builds::BuildLayout& layout);

}  // namespace rb::play
