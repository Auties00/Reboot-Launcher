#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string_view>
#include <variant>

#include "engine_start_rules.hpp"
#include "messages.hpp"

using reboot::ArgSpec;
using reboot::DataRoot;
using reboot::Diagnostic;
using reboot::MessageSpec;
using reboot::NativePath;
using reboot::Result;
using reboot::os_macos::ipc::agent_register_failed;
using reboot::os_macos::ipc::agent_register_timed_out;
using reboot::os_macos::ipc::agent_step;
using reboot::os_macos::ipc::AgentStatus;
using reboot::os_macos::ipc::AgentStep;
using reboot::os_macos::ipc::kickstart_outcome;
using reboot::os_macos::ipc::kLaunchctlServiceDisabled;
using reboot::os_macos::ipc::kLaunchctlServiceNotFound;
using reboot::os_macos::ipc::LaunchctlRun;
using reboot::os_macos::ipc::refusal_before_start;
using reboot::ports::CallerContext;
using reboot::ports::StartResult;

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
    for (const MessageSpec* spec : reboot::message_registry())
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
    CHECK(agent_step(AgentStatus::Enabled) == AgentStep::Kickstart);
    CHECK(agent_step(AgentStatus::RequiresApproval) == AgentStep::AwaitingUser);
    CHECK(agent_step(AgentStatus::NotRegistered) == AgentStep::Register);
    CHECK(agent_step(AgentStatus::NotFound) == AgentStep::Register);
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
        CHECK(result.error().is(reboot::os_macos::ipc::kAgentKickstartFailed));
        CHECK_FALSE(result.error().retryable);
        CHECK(result.error().detail.has_value());
        check_placeholders(result.error());
    }
}

TEST_CASE("a launchctl ended by a signal is agent_kickstart_failed", "[engine_start_rules]") {
    const Result<StartResult> result = outcome(LaunchctlRun::End::Signalled, 9);
    REQUIRE_FALSE(result);
    CHECK(result.error().is(reboot::os_macos::ipc::kAgentKickstartFailed));
    check_placeholders(result.error());
}

TEST_CASE("a kickstart past its deadline is retryable and names the deadline", "[engine_start_rules]") {
    const Result<StartResult> result = outcome(LaunchctlRun::End::TimedOut, 0);
    REQUIRE_FALSE(result);
    CHECK(result.error().is(reboot::os_macos::ipc::kAgentKickstartTimedOut));
    CHECK(result.error().retryable);
    const reboot::Arg* deadline = result.error().find_arg("deadline");
    REQUIRE(deadline != nullptr);
    REQUIRE(std::holds_alternative<std::chrono::milliseconds>(*deadline));
    CHECK(std::get<std::chrono::milliseconds>(*deadline) == kDeadline);
    check_placeholders(result.error());
}

TEST_CASE("a register failure carries the NSError code when there is one", "[engine_start_rules]") {
    const Diagnostic with_code = agent_register_failed(kLabel, 1);
    CHECK(with_code.is(reboot::os_macos::ipc::kAgentRegisterFailed));
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
    CHECK(diag.is(reboot::os_macos::ipc::kAgentRegisterTimedOut));
    CHECK(diag.retryable);
    check_placeholders(diag);
}
