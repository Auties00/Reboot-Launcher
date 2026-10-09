#include <catch2/catch_test_macros.hpp>

#include <string>

#include "reboot/play/play_session_state.hpp"

using namespace reboot;
using namespace reboot::play;
namespace gc = reboot::contracts::game_client;

namespace {

std::string arg_of(const Diagnostic& diag, std::string_view name) {
    const Arg* arg = diag.find_arg(name);
    REQUIRE(arg != nullptr);
    return std::get<std::string>(*arg);
}

PlaySessionState at(PlayPhase phase) {
    PlaySessionState state;
    state.phase = phase;
    return state;
}

}  // namespace

TEST_CASE("our DLL's events walk the phases to Running", "[play][state]") {
    PlaySessionState state = at(PlayPhase::Loading);
    CHECK_FALSE(apply(state, game_channel::Loaded{"1.0", 3, gc::BuildMatch::Exact}).exit);
    CHECK(state.phase == PlayPhase::Loaded);
    CHECK_FALSE(apply(state, game_channel::RedirectReady{}).exit);
    CHECK(state.phase == PlayPhase::RedirectReady);
    CHECK_FALSE(apply(state, game_channel::LoggedIn{}).exit);
    CHECK(state.phase == PlayPhase::Running);
    // A late Loaded never takes the session back.
    (void)apply(state, game_channel::Loaded{});
    CHECK(state.phase == PlayPhase::Running);
}

TEST_CASE("the registry sees Loading for every loading step", "[play][state]") {
    CHECK(session_phase(PlayPhase::Preparing) == sessions::SessionPhase::Preparing);
    CHECK(session_phase(PlayPhase::Launching) == sessions::SessionPhase::Launching);
    CHECK(session_phase(PlayPhase::Loading) == sessions::SessionPhase::Loading);
    CHECK(session_phase(PlayPhase::Loaded) == sessions::SessionPhase::Loading);
    CHECK(session_phase(PlayPhase::RedirectReady) == sessions::SessionPhase::Loading);
    CHECK(session_phase(PlayPhase::Running) == sessions::SessionPhase::Running);
}

TEST_CASE("an exit request is ignored before Running and ends the session after", "[play][state]") {
    PlaySessionState loading = at(PlayPhase::Loaded);
    CHECK_FALSE(apply(loading, game_channel::ExitRequested{gc::ExitKind::RequestExit, 0, "boot"}).exit);
    CHECK_FALSE(loading.ending);

    PlaySessionState running = at(PlayPhase::Running);
    const PlayEventEffect effect = apply(running, game_channel::ExitRequested{gc::ExitKind::RequestExit, 3, ""});
    REQUIRE(effect.exit);
    CHECK(effect.exit->reason == sessions::ExitReason::Exited);
    CHECK(effect.exit->exit_code == 3);
    CHECK_FALSE(effect.exit->error);
    CHECK(running.ending);
}

TEST_CASE("a required hook failure is fatal and an optional one degrades", "[play][state]") {
    PlaySessionState state = at(PlayPhase::Loaded);
    PlayEventEffect optional = apply(state, game_channel::HookFailed{"console", false});
    CHECK_FALSE(optional.exit);
    REQUIRE(optional.degraded);
    CHECK(optional.degraded->id == "play.features_degraded");
    CHECK(optional.degraded->severity == Severity::Warning);
    CHECK(arg_of(*optional.degraded, "features") == "console");

    PlayEventEffect patch = apply(state, game_channel::PatchResult{"memory_fix", gc::PatchStatus::NotFound, 0});
    REQUIRE(patch.degraded);
    CHECK(arg_of(*patch.degraded, "features") == "console, memory_fix");
    // The same id again, or a patch that applied, changes nothing.
    CHECK_FALSE(apply(state, game_channel::PatchResult{"memory_fix", gc::PatchStatus::Failed, 0}).degraded);
    CHECK_FALSE(apply(state, game_channel::PatchResult{"other", gc::PatchStatus::Applied, 0}).degraded);
    CHECK_FALSE(apply(state, game_channel::PatchResult{"other", gc::PatchStatus::Skipped, 0}).degraded);
    CHECK(state.degraded == std::vector<std::string>{"console", "memory_fix"});

    PlayEventEffect required = apply(state, game_channel::HookFailed{"process_request", true});
    REQUIRE(required.exit);
    CHECK(required.exit->reason == sessions::ExitReason::Fatal);
    CHECK(required.exit->error->id == "play.hook_failed");
    CHECK(arg_of(*required.exit->error, "step") == "process_request");
}

TEST_CASE("fatal causes map to their diagnostics", "[play][state]") {
    PlaySessionState dll = at(PlayPhase::Loaded);
    PlayEventEffect step = apply(dll, game_channel::SessionFatal{game_channel::FatalCause::DllStep, "unpack"});
    REQUIRE(step.exit);
    CHECK(step.exit->reason == sessions::ExitReason::Fatal);
    CHECK(step.exit->error->id == "play.fatal");
    CHECK(arg_of(*step.exit->error, "step") == "unpack");

    PlaySessionState early = at(PlayPhase::Loading);
    CHECK(apply(early, game_channel::SessionFatal{game_channel::FatalCause::CorruptBuild, ""}).exit->error->id ==
          "play.corrupt_build");
    PlaySessionState late = at(PlayPhase::Running);
    const PlayEventEffect crashed = apply(late, game_channel::SessionFatal{game_channel::FatalCause::CorruptBuild, ""});
    CHECK(crashed.exit->reason == sessions::ExitReason::Crashed);
    CHECK(crashed.exit->error->id == "play.crashed");

    PlaySessionState auth = at(PlayPhase::Loaded);
    CHECK(apply(auth, game_channel::SessionFatal{game_channel::FatalCause::AuthFailure, ""}).exit->error->id ==
          "play.auth_failure");
    PlaySessionState connect = at(PlayPhase::Loaded);
    CHECK(apply(connect, game_channel::SessionFatal{game_channel::FatalCause::CannotConnect, ""}).exit->error->id ==
          "play.cannot_connect");
}

TEST_CASE("nothing changes once the session is ending", "[play][state]") {
    PlaySessionState state = at(PlayPhase::Loaded);
    state.ending = true;
    CHECK_FALSE(apply(state, game_channel::LoggedIn{}).exit);
    CHECK(state.phase == PlayPhase::Loaded);
    CHECK_FALSE(apply(state, game_channel::SessionFatal{game_channel::FatalCause::DllStep, "x"}).exit);
    CHECK_FALSE(apply(state, game_channel::HookFailed{"x", false}).degraded);
}

TEST_CASE("console readiness and travel are tracked", "[play][state]") {
    PlaySessionState state = at(PlayPhase::Running);
    (void)apply(state, game_channel::ConsoleReady{});
    CHECK(state.console_ready);
    (void)apply(state, game_channel::TravelStarted{});
    CHECK(state.traveling);
    (void)apply(state, game_channel::TravelEnded{});
    CHECK_FALSE(state.traveling);
    CHECK_FALSE(apply(state, game_channel::WindowCreated{42}).exit);
    CHECK_FALSE(apply(state, game_channel::Joined{"1.2.3.4:7777"}).exit);
    CHECK_FALSE(apply(state, game_channel::Disconnected{"timeout"}).exit);
}

TEST_CASE("a game exit before login is Exited with exited_before_login", "[play][state]") {
    const sessions::SessionExit exit = exit_for_game_exit(at(PlayPhase::Loaded), 0);
    CHECK(exit.reason == sessions::ExitReason::Exited);
    CHECK(exit.exit_code == 0);
    REQUIRE(exit.error);
    CHECK(exit.error->id == "play.exited_before_login");
}

TEST_CASE("after login an NTSTATUS error code is a crash and anything else an exit", "[play][state]") {
    const PlaySessionState running = at(PlayPhase::Running);
    const sessions::SessionExit crash = exit_for_game_exit(running, static_cast<i32>(0xC0000005u));
    CHECK(crash.reason == sessions::ExitReason::Crashed);
    CHECK(crash.error->id == "play.crashed");
    CHECK(exit_for_game_exit(running, static_cast<i32>(0xBFFFFFFFu)).reason == sessions::ExitReason::Exited);
    const sessions::SessionExit clean = exit_for_game_exit(running, 0);
    CHECK(clean.reason == sessions::ExitReason::Exited);
    CHECK_FALSE(clean.error);
    CHECK(exit_for_game_exit(running, std::nullopt).reason == sessions::ExitReason::Exited);
}
