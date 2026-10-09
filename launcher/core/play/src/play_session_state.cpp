#include "reboot/play/play_session_state.hpp"

#include <algorithm>
#include <concepts>
#include <utility>

#include "messages.hpp"

namespace rb::play {

namespace {

namespace gc = contracts::game_client;

// NTSTATUS values from 0xC0000000 up are errors: an access violation, a stack overflow and so on.
constexpr u32 kNtStatusError = 0xC0000000u;

[[nodiscard]] Diagnostic play_diag(MessageId message) { return make_diag(ErrorDomain::Play, message).build(); }

[[nodiscard]] Diagnostic step_diag(MessageId message, std::string_view step) {
    return make_diag(ErrorDomain::Play, message).arg("step", step).build();
}

[[nodiscard]] bool running(const PlaySessionState& state) noexcept { return state.phase == PlayPhase::Running; }

[[nodiscard]] sessions::SessionExit fatal(Diagnostic error) {
    return sessions::SessionExit{.reason = sessions::ExitReason::Fatal, .exit_code = std::nullopt, .error = std::move(error)};
}

[[nodiscard]] Diagnostic degraded_diag(const std::vector<std::string>& ids) {
    std::string features;
    for (const std::string& id : ids) {
        if (!features.empty()) features += ", ";
        features += id;
    }
    return make_diag(ErrorDomain::Play, msg::kFeaturesDegraded).arg("features", features).severity(Severity::Warning).build();
}

void add_degraded(PlaySessionState& state, PlayEventEffect& effect, std::string id) {
    if (std::ranges::find(state.degraded, id) != state.degraded.end()) return;
    state.degraded.push_back(std::move(id));
    effect.degraded = degraded_diag(state.degraded);
}

// Phases only move forward, so a late or repeated event never takes the session back.
void advance(PlaySessionState& state, PlayPhase phase) noexcept {
    if (state.phase < phase) state.phase = phase;
}

[[nodiscard]] sessions::SessionExit fatal_exit(const PlaySessionState& state, const game_channel::SessionFatal& event) {
    switch (event.cause) {
        case game_channel::FatalCause::DllStep: return fatal(step_diag(msg::kFatal, event.step));
        case game_channel::FatalCause::CorruptBuild:
            if (running(state))
                return sessions::SessionExit{
                    .reason = sessions::ExitReason::Crashed, .exit_code = std::nullopt, .error = play_diag(msg::kCrashed)};
            return fatal(play_diag(msg::kCorruptBuild));
        case game_channel::FatalCause::AuthFailure: return fatal(play_diag(msg::kAuthFailure));
        case game_channel::FatalCause::CannotConnect: return fatal(play_diag(msg::kCannotConnect));
    }
    return fatal(play_diag(msg::kFatal));
}

}  // namespace

PlayEventEffect apply(PlaySessionState& state, const game_channel::GameLifecycleEvent& event) {
    PlayEventEffect effect;
    if (state.ending) return effect;
    std::visit(
        [&]<class E>(const E& payload) {
            if constexpr (std::same_as<E, game_channel::Loaded>) {
                advance(state, PlayPhase::Loaded);
            } else if constexpr (std::same_as<E, game_channel::PatchResult>) {
                if (payload.status == gc::PatchStatus::Failed || payload.status == gc::PatchStatus::NotFound)
                    add_degraded(state, effect, payload.id);
            } else if constexpr (std::same_as<E, game_channel::RedirectReady>) {
                advance(state, PlayPhase::RedirectReady);
            } else if constexpr (std::same_as<E, game_channel::HookFailed>) {
                if (payload.required) effect.exit = fatal(step_diag(msg::kHookFailed, payload.step));
                else add_degraded(state, effect, payload.step);
            } else if constexpr (std::same_as<E, game_channel::LoggedIn>) {
                advance(state, PlayPhase::Running);
            } else if constexpr (std::same_as<E, game_channel::ExitRequested>) {
                if (running(state))
                    effect.exit = sessions::SessionExit{
                        .reason = sessions::ExitReason::Exited, .exit_code = payload.code, .error = std::nullopt};
            } else if constexpr (std::same_as<E, game_channel::ConsoleReady>) {
                state.console_ready = true;
            } else if constexpr (std::same_as<E, game_channel::TravelStarted>) {
                state.traveling = true;
            } else if constexpr (std::same_as<E, game_channel::TravelEnded>) {
                state.traveling = false;
            } else if constexpr (std::same_as<E, game_channel::SessionFatal>) {
                effect.exit = fatal_exit(state, payload);
            }
        },
        event);
    if (effect.exit) state.ending = true;
    return effect;
}

sessions::SessionExit exit_for_game_exit(const PlaySessionState& state, std::optional<i32> exit_code) {
    if (!running(state))
        return sessions::SessionExit{
            .reason = sessions::ExitReason::Exited, .exit_code = exit_code, .error = play_diag(msg::kExitedBeforeLogin)};
    if (exit_code && static_cast<u32>(*exit_code) >= kNtStatusError)
        return sessions::SessionExit{
            .reason = sessions::ExitReason::Crashed, .exit_code = exit_code, .error = play_diag(msg::kCrashed)};
    return sessions::SessionExit{.reason = sessions::ExitReason::Exited, .exit_code = exit_code, .error = std::nullopt};
}

}  // namespace rb::play
