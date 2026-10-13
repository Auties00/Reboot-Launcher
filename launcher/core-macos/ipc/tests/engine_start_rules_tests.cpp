#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "engine_start_rules.hpp"
#include "messages.hpp"
#include "reboot/foundation/diag.hpp"

using rb::ArgSpec;
using rb::DataRoot;
using rb::Diagnostic;
using rb::MessageSpec;
using rb::NativePath;
using rb::Result;
using rb::os_macos::ipc::agent_register_failed;
using rb::os_macos::ipc::agent_register_timed_out;
using rb::os_macos::ipc::agent_step;
using rb::os_macos::ipc::app_translocated;
using rb::os_macos::ipc::AgentStatus;
using rb::os_macos::ipc::AgentStep;
using rb::os_macos::ipc::kickstart_outcome;
using rb::os_macos::ipc::kickstart_retry_after_register;
using rb::os_macos::ipc::kickstart_with_retries;
using rb::os_macos::ipc::KickstartCalls;
using rb::os_macos::ipc::kKickstartMinTry;
using rb::os_macos::ipc::kLaunchctlServiceDisabled;
using rb::os_macos::ipc::kLaunchctlServiceNotFound;
using rb::os_macos::ipc::LaunchctlRun;
using rb::os_macos::ipc::refusal_before_start;
using rb::ports::CallerContext;
using rb::ports::StartResult;

namespace {

constexpr std::string_view kLabel = "dev.projectreboot.launcher.engine";
constexpr std::chrono::milliseconds kDeadline{3000};

DataRoot default_root() {
    return {.root = NativePath{"/Users/me/Library/Application Support/Reboot Launcher"}, .overridden = false};
}

DataRoot overridden_root() { return {.root = NativePath{"/Users/me/reboot-home"}, .overridden = true}; }

CallerContext caller(bool elevated, bool interactive) {
    CallerContext context;
    context.os_session = "100008";
    context.elevated = elevated;
    context.interactive = interactive;
    return context;
}

const MessageSpec* find_spec(std::string_view id) {
    for (const MessageSpec* spec : rb::message_registry())
        if (spec->id == id) return spec;
    return nullptr;
}

void check_placeholders(const Diagnostic& diag) {
    INFO(diag.id);
    const MessageSpec* spec = find_spec(diag.id);
    REQUIRE(spec != nullptr);
    CHECK(diag.args.size() == spec->args.size());
    for (const ArgSpec& arg : spec->args) {
        INFO(arg.name);
        CHECK(diag.find_arg(arg.name) != nullptr);
    }
}

Result<StartResult> outcome(LaunchctlRun::End end, int code) {
    return kickstart_outcome(LaunchctlRun{.end = end, .code = code}, kLabel, kDeadline);
}

// launchctl under a scripted clock: each try takes `try_takes` and exits with the next code.
struct ScriptedKickstart {
    std::vector<int> exits;
    std::chrono::milliseconds try_takes{0};
    std::chrono::milliseconds pause_takes{0};
    std::chrono::steady_clock::time_point clock{};
    std::vector<std::chrono::milliseconds> deadlines;
    int pauses = 0;

    KickstartCalls calls() {
        return KickstartCalls{
            .run = [this](std::chrono::milliseconds deadline) -> Result<LaunchctlRun> {
                deadlines.push_back(deadline);
                clock += try_takes;
                const int code = exits.at(deadlines.size() - 1);
                return LaunchctlRun{.end = LaunchctlRun::End::Exited, .code = code};
            },
            .pause = [this](std::chrono::milliseconds) {
                ++pauses;
                clock += pause_takes;
            },
            .now = [this] { return clock; }};
    }
};

constexpr std::chrono::milliseconds kInterval{100};

}  // namespace

TEST_CASE("an elevated caller is refused before anything else", "[engine_start_rules]") {
    CHECK(refusal_before_start(caller(true, false), overridden_root()) == StartResult::ElevatedRefused);
    CHECK(refusal_before_start(caller(true, true), default_root()) == StartResult::ElevatedRefused);
}

TEST_CASE("outside the Aqua session nothing starts", "[engine_start_rules]") {
    CHECK(refusal_before_start(caller(false, false), overridden_root()) == StartResult::NoInteractiveSession);
    CHECK(refusal_before_start(caller(false, false), default_root()) == StartResult::NoInteractiveSession);
}

TEST_CASE("the one agent cannot serve an overridden root", "[engine_start_rules]") {
    CHECK(refusal_before_start(caller(false, true), overridden_root()) == StartResult::CannotDetach);
}

TEST_CASE("an interactive caller on the default root goes on", "[engine_start_rules]") {
    CHECK(refusal_before_start(caller(false, true), default_root()) == std::nullopt);
}

TEST_CASE("each agent status maps to one step", "[engine_start_rules]") {
    CHECK(agent_step(AgentStatus::Enabled, false) == AgentStep::Kickstart);
    CHECK(agent_step(AgentStatus::RequiresApproval, false) == AgentStep::AwaitingUser);
    CHECK(agent_step(AgentStatus::NotRegistered, false) == AgentStep::Register);
    CHECK(agent_step(AgentStatus::NotFound, false) == AgentStep::Register);
}

TEST_CASE("a translocated app registers nothing but starts an agent already there", "[engine_start_rules]") {
    CHECK(agent_step(AgentStatus::NotRegistered, true) == AgentStep::Translocated);
    CHECK(agent_step(AgentStatus::NotFound, true) == AgentStep::Translocated);
    CHECK(agent_step(AgentStatus::Enabled, true) == AgentStep::Kickstart);
    CHECK(agent_step(AgentStatus::RequiresApproval, true) == AgentStep::AwaitingUser);

    const Diagnostic diag = app_translocated();
    CHECK(diag.is(rb::os_macos::ipc::kAppTranslocated));
    CHECK(diag.kind == rb::ErrorKind::Conflict);
    check_placeholders(diag);
}

TEST_CASE("only an unknown label right after registering is kickstarted again", "[engine_start_rules]") {
    const LaunchctlRun unknown{.end = LaunchctlRun::End::Exited, .code = kLaunchctlServiceNotFound};
    CHECK(kickstart_retry_after_register(unknown, true));
    CHECK_FALSE(kickstart_retry_after_register(unknown, false));
    CHECK_FALSE(kickstart_retry_after_register(LaunchctlRun{.end = LaunchctlRun::End::Exited, .code = 0}, true));
    CHECK_FALSE(kickstart_retry_after_register(
        LaunchctlRun{.end = LaunchctlRun::End::Exited, .code = kLaunchctlServiceDisabled}, true));
    CHECK_FALSE(kickstart_retry_after_register(LaunchctlRun{.end = LaunchctlRun::End::TimedOut, .code = 0}, true));
    CHECK_FALSE(kickstart_retry_after_register(
        LaunchctlRun{.end = LaunchctlRun::End::Signalled, .code = kLaunchctlServiceNotFound}, true));
}

TEST_CASE("a label launchd loads late is kicked again within what is left of the budget", "[engine_start_rules]") {
    ScriptedKickstart launchctl{.exits = {kLaunchctlServiceNotFound, kLaunchctlServiceNotFound, 0},
                                .try_takes = std::chrono::milliseconds{50},
                                .pause_takes = std::chrono::milliseconds{120}};
    KickstartCalls calls = launchctl.calls();
    const Result<LaunchctlRun> run = kickstart_with_retries(calls, true, kDeadline, kInterval);
    REQUIRE(run);
    CHECK(run->code == 0);
    // The second pause overshot by 20 ms; each try gets only what is left after it.
    const std::vector<std::chrono::milliseconds> expected{kDeadline, std::chrono::milliseconds{2830}, std::chrono::milliseconds{2660}};
    CHECK(launchctl.deadlines == expected);
}

TEST_CASE("a pause that overshoots the budget leaves the unknown label standing", "[engine_start_rules]") {
    ScriptedKickstart launchctl{.exits = {kLaunchctlServiceNotFound, kLaunchctlServiceNotFound},
                                .try_takes = std::chrono::milliseconds{2000},
                                .pause_takes = std::chrono::milliseconds{1500}};
    KickstartCalls calls = launchctl.calls();
    const Result<LaunchctlRun> run = kickstart_with_retries(calls, true, kDeadline, kInterval);
    REQUIRE(run);
    CHECK(launchctl.pauses == 1);
    CHECK(launchctl.deadlines.size() == 1);
    CHECK(run->code == kLaunchctlServiceNotFound);
    const Result<StartResult> result = kickstart_outcome(*run, kLabel, kDeadline);
    REQUIRE(result);
    CHECK(*result == StartResult::AwaitingUser);
}

TEST_CASE("no retry is made with less than the shortest try left", "[engine_start_rules]") {
    // 2700 ms gone leaves 300 ms: the pause would leave under kKickstartMinTry.
    ScriptedKickstart launchctl{.exits = {kLaunchctlServiceNotFound}, .try_takes = std::chrono::milliseconds{2700}};
    KickstartCalls calls = launchctl.calls();
    const Result<LaunchctlRun> run = kickstart_with_retries(calls, true, kDeadline, kInterval);
    REQUIRE(run);
    CHECK(launchctl.pauses == 0);
    CHECK(launchctl.deadlines.size() == 1);
    CHECK(run->code == kLaunchctlServiceNotFound);
    STATIC_CHECK(kKickstartMinTry + kInterval > std::chrono::milliseconds{300});
}

TEST_CASE("a label this call did not register is kicked once", "[engine_start_rules]") {
    ScriptedKickstart launchctl{.exits = {kLaunchctlServiceNotFound}};
    KickstartCalls calls = launchctl.calls();
    const Result<LaunchctlRun> run = kickstart_with_retries(calls, false, kDeadline, kInterval);
    REQUIRE(run);
    CHECK(launchctl.deadlines.size() == 1);
    CHECK(launchctl.pauses == 0);
}

TEST_CASE("a clean kickstart exit is Started", "[engine_start_rules]") {
    const Result<StartResult> result = outcome(LaunchctlRun::End::Exited, 0);
    REQUIRE(result);
    CHECK(*result == StartResult::Started);
}

TEST_CASE("a label the domain does not know awaits the user", "[engine_start_rules]") {
    const Result<StartResult> result = outcome(LaunchctlRun::End::Exited, kLaunchctlServiceNotFound);
    REQUIRE(result);
    CHECK(*result == StartResult::AwaitingUser);
}

TEST_CASE("an agent switched off in Login Items awaits the user", "[engine_start_rules]") {
    // The CLI never reads the SMAppService status, so only launchctl tells it.
    const Result<StartResult> result = outcome(LaunchctlRun::End::Exited, kLaunchctlServiceDisabled);
    REQUIRE(result);
    CHECK(*result == StartResult::AwaitingUser);
}

TEST_CASE("any other exit is agent_kickstart_failed and not retryable", "[engine_start_rules]") {
    for (const int code : {1, 5, 37, 112, 114, 118, 120, 125}) {
        INFO(code);
        const Result<StartResult> result = outcome(LaunchctlRun::End::Exited, code);
        REQUIRE_FALSE(result);
        CHECK(result.error().is(rb::os_macos::ipc::kAgentKickstartFailed));
        CHECK_FALSE(result.error().retryable);
        CHECK(result.error().detail.has_value());
        check_placeholders(result.error());
    }
}

TEST_CASE("a launchctl ended by a signal is agent_kickstart_failed", "[engine_start_rules]") {
    const Result<StartResult> result = outcome(LaunchctlRun::End::Signalled, 9);
    REQUIRE_FALSE(result);
    CHECK(result.error().is(rb::os_macos::ipc::kAgentKickstartFailed));
    check_placeholders(result.error());
}

TEST_CASE("a kickstart past its deadline is retryable and names the deadline", "[engine_start_rules]") {
    const Result<StartResult> result = outcome(LaunchctlRun::End::TimedOut, 0);
    REQUIRE_FALSE(result);
    CHECK(result.error().is(rb::os_macos::ipc::kAgentKickstartTimedOut));
    CHECK(result.error().retryable);
    const rb::Arg* deadline = result.error().find_arg("deadline");
    REQUIRE(deadline != nullptr);
    REQUIRE(std::holds_alternative<std::chrono::milliseconds>(*deadline));
    CHECK(std::get<std::chrono::milliseconds>(*deadline) == kDeadline);
    check_placeholders(result.error());
}

TEST_CASE("a register failure carries the NSError code when there is one", "[engine_start_rules]") {
    const Diagnostic with_code = agent_register_failed(kLabel, 1);
    CHECK(with_code.is(rb::os_macos::ipc::kAgentRegisterFailed));
    REQUIRE(with_code.os_error);
    CHECK(with_code.os_error->code == 1);
    CHECK_FALSE(with_code.retryable);
    check_placeholders(with_code);

    const Diagnostic without_code = agent_register_failed(kLabel, std::nullopt);
    CHECK_FALSE(without_code.os_error);
    check_placeholders(without_code);
}

TEST_CASE("a register past its deadline is retryable", "[engine_start_rules]") {
    const Diagnostic diag = agent_register_timed_out(kLabel, kDeadline);
    CHECK(diag.is(rb::os_macos::ipc::kAgentRegisterTimedOut));
    CHECK(diag.retryable);
    check_placeholders(diag);
}
