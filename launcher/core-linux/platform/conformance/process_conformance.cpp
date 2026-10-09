#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <signal.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>

#include "conformance_support.hpp"
#include "helper_process.hpp"
#include "messages.hpp"
#include "proc_stat.hpp"
#include "reboot/os_linux/platform/pidfd_process_launcher.hpp"
#include "text_files.hpp"

using namespace reboot;
using namespace reboot::os_linux::platform;
using reboot::os_linux::platform::test::require_passed;
using reboot::os_linux::platform::test::Scratch;
using namespace std::chrono_literals;

namespace {

// Running, not a zombie waiting for its parent.
[[nodiscard]] bool process_exists(u32 pid) {
    const std::optional<std::string> stat = try_read_text_file(NativePath{"/proc"} / std::to_string(pid) / "stat");
    const std::optional<ProcStat> parsed = stat ? parse_proc_stat(*stat) : std::nullopt;
    return parsed && parsed->state != 'Z';
}

[[nodiscard]] ports::ProcessLaunch shell(std::string script) {
    ports::ProcessLaunch launch;
    launch.exe = "/bin/sh";
    launch.args = {"-c", std::move(script)};
    launch.env.vars.emplace_back("PATH", "/usr/bin:/bin");
    return launch;
}

struct Watched {
    std::mutex mutex;
    std::string out;
    std::optional<ports::ChildExit> exit;
    int exits = 0;
};

void watch(ports::ChildProcess& child, const std::shared_ptr<Watched>& state) {
    child.on_stdout([state](std::span<const u8> bytes) {
        const std::lock_guard lock(state->mutex);
        state->out.append(bytes.begin(), bytes.end());
    });
    child.on_exit([state](ports::ChildExit exit) {
        const std::lock_guard lock(state->mutex);
        state->exit = exit;
        ++state->exits;
    });
}

[[nodiscard]] bool exited(Scratch& scratch, const std::shared_ptr<Watched>& state) {
    return scratch.waiter.wait_until(
        [&] {
            const std::lock_guard lock(state->mutex);
            return state->exit.has_value();
        },
        10s);
}

}  // namespace

TEST_CASE("PidfdProcessLauncher passes the process launcher suite", "[linux_conformance]") {
    Scratch scratch;
    PidfdProcessLauncher launcher(std::nullopt);
    testing::ProcessConformanceSubject subject;
    subject.exits_with_7 = shell("exit 7");
    subject.echoes_stdin = shell("cat");
    subject.writes_stderr = shell("echo oops >&2");
    subject.prints_marker_and_cwd = shell("echo \"$REBOOT_CONFORMANCE_MARKER\"; pwd");
    subject.spawns_grandchild = shell("sleep 1000 & echo $!; wait");
    subject.process_exists = process_exists;
    require_passed(testing::run_process_launcher_conformance(launcher, std::move(subject), scratch.env()));
}

TEST_CASE("output written before anyone listens is delivered, then the exit", "[linux_conformance]") {
    Scratch scratch;
    PidfdProcessLauncher launcher(std::nullopt);
    ports::ProcessLaunch launch = shell("echo early");
    launch.stdio = ports::StdioMode::Capture;
    auto child = launcher.spawn(launch);
    REQUIRE(child);
    // The child has long exited when the callbacks arrive.
    (void)scratch.waiter.wait_until([&] { return !process_exists((*child)->pid()); }, 5s);
    auto state = std::make_shared<Watched>();
    watch(**child, state);
    REQUIRE(exited(scratch, state));
    const std::lock_guard lock(state->mutex);
    CHECK(state->out == "early\n");
    CHECK(state->exit->code == 0);
    CHECK(state->exits == 1);
}

TEST_CASE("a signal that ends a child is reported as such", "[linux_conformance]") {
    Scratch scratch;
    PidfdProcessLauncher launcher(std::nullopt);
    auto child = launcher.spawn(shell("kill -TERM $$"));
    REQUIRE(child);
    auto state = std::make_shared<Watched>();
    watch(**child, state);
    REQUIRE(exited(scratch, state));
    const std::lock_guard lock(state->mutex);
    CHECK(state->exit->signal == SIGTERM);
    CHECK_FALSE(state->exit->code);
}

TEST_CASE("a program that cannot be executed fails the spawn", "[linux_conformance]") {
    PidfdProcessLauncher launcher(std::nullopt);
    ports::ProcessLaunch launch;
    launch.exe = "/nonexistent/reboot-missing";
    const auto missing = launcher.spawn(launch);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().kind == ErrorKind::NotFound);
    REQUIRE(missing.error().os_error);
    CHECK(missing.error().os_error->code == ENOENT);

    ports::ProcessLaunch bad_cwd = shell("exit 0");
    bad_cwd.cwd = "/nonexistent/dir";
    CHECK_FALSE(launcher.spawn(bad_cwd));
}

TEST_CASE("children lead their own group unless asked not to, and ignore the engine's signal setup", "[linux_conformance]") {
    Scratch scratch;
    PidfdProcessLauncher launcher(std::nullopt);
    ports::ProcessLaunch own = shell("cat");
    own.stdio = ports::StdioMode::ControlChannel;
    auto leader = launcher.spawn(own);
    REQUIRE(leader);
    CHECK(::getpgid(static_cast<pid_t>((*leader)->pid())) == static_cast<pid_t>((*leader)->pid()));

    ports::ProcessLaunch shared = own;
    shared.own_group = false;
    auto member = launcher.spawn(shared);
    REQUIRE(member);
    CHECK(::getpgid(static_cast<pid_t>((*member)->pid())) == ::getpgrp());

    // The child's mask and dispositions are reset, so a SIGPIPE the engine ignores ends it.
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    struct sigaction previous {};
    ::sigaction(SIGPIPE, &ignore, &previous);
    auto piped = launcher.spawn(shell("kill -PIPE $$"));
    ::sigaction(SIGPIPE, &previous, nullptr);
    REQUIRE(piped);
    auto state = std::make_shared<Watched>();
    watch(**piped, state);
    REQUIRE(exited(scratch, state));
    {
        const std::lock_guard lock(state->mutex);
        CHECK(state->exit->signal == SIGPIPE);
    }
    REQUIRE((*leader)->terminate_tree());
    REQUIRE((*member)->terminate_tree());
}

TEST_CASE("terminate_tree kills what is left of the group after its leader exited", "[linux_conformance]") {
    Scratch scratch;
    PidfdProcessLauncher launcher(std::nullopt);
    ports::ProcessLaunch launch = shell("sleep 1000 >/dev/null 2>&1 & echo $!");
    launch.stdio = ports::StdioMode::Capture;
    auto child = launcher.spawn(launch);
    REQUIRE(child);
    auto state = std::make_shared<Watched>();
    watch(**child, state);
    REQUIRE(exited(scratch, state));
    u32 orphan = 0;
    {
        const std::lock_guard lock(state->mutex);
        orphan = static_cast<u32>(std::stoul(state->out));
    }
    REQUIRE(process_exists(orphan));
    CHECK(::getpgid(static_cast<pid_t>(orphan)) == static_cast<pid_t>((*child)->pid()));
    REQUIRE((*child)->terminate_tree());
    CHECK(scratch.waiter.wait_until([&] { return !process_exists(orphan); }, 10s));
    // The leader stays a zombie until its handle goes, then is reaped.
    const u32 leader = (*child)->pid();
    child->reset();
    CHECK(scratch.waiter.wait_until([&] { return !read_proc_start_time(leader).value_or(std::nullopt); }, 10s));
}

TEST_CASE("destroying a child closes its stdin, and the launcher still reaps it", "[linux_conformance]") {
    Scratch scratch;
    PidfdProcessLauncher launcher(std::nullopt);
    ports::ProcessLaunch launch = shell("cat");
    launch.stdio = ports::StdioMode::ControlChannel;
    auto child = launcher.spawn(launch);
    REQUIRE(child);
    const u32 pid = (*child)->pid();
    child->reset();
    CHECK(scratch.waiter.wait_until([&] { return !process_exists(pid); }, 10s));
}

TEST_CASE("children die with the launcher's spawning thread", "[linux_conformance]") {
    Scratch scratch;
    u32 pid = 0;
    {
        PidfdProcessLauncher launcher(std::nullopt);
        auto child = launcher.spawn(shell("sleep 1000"));
        REQUIRE(child);
        pid = (*child)->pid();
        CHECK(process_exists(pid));
    }
    CHECK(scratch.waiter.wait_until([&] { return !process_exists(pid); }, 10s));
}

TEST_CASE("a scope name without a systemd user manager runs the command directly", "[linux_conformance]") {
    Scratch scratch;
    PidfdProcessLauncher launcher(std::nullopt);
    ports::ProcessLaunch launch = shell("echo direct");
    launch.stdio = ports::StdioMode::Capture;
    launch.scope_name = "reboot-conformance";
    auto child = launcher.spawn(launch);
    REQUIRE(child);
    auto state = std::make_shared<Watched>();
    watch(**child, state);
    REQUIRE(exited(scratch, state));
    const std::lock_guard lock(state->mutex);
    CHECK(state->out == "direct\n");
}

TEST_CASE("start times come from /proc and are absent for a missing pid", "[linux_conformance]") {
    const auto own = read_proc_start_time(static_cast<u32>(::getpid()));
    REQUIRE(own);
    REQUIRE(own->has_value());
    CHECK(**own <= std::chrono::system_clock::now() + 2s);
    const auto missing = read_proc_start_time(0x7FFFFFF0U);
    REQUIRE(missing);
    CHECK_FALSE(missing->has_value());
}

TEST_CASE("helpers report their exit code and output", "[linux_conformance]") {
    HelperCommand command;
    command.program = "sh";
    command.args = {"-c", "echo hello; exit 3"};
    command.capture_stdout = true;
    const Result<HelperResult> ran = run_helper(command);
    REQUIRE(ran);
    CHECK(ran->exit_code == 3);
    CHECK(ran->output == "hello\n");
    CHECK(helper_failed("sh", *ran).is(kHelperFailed));
}

TEST_CASE("a helper past its deadline is killed or left running", "[linux_conformance]") {
    HelperCommand slow;
    slow.program = "sleep";
    slow.args = {"30"};
    const auto began = std::chrono::steady_clock::now();
    const Result<HelperResult> killed = run_helper(slow, 200ms);
    CHECK(std::chrono::steady_clock::now() - began < 10s);
    REQUIRE_FALSE(killed);
    CHECK(killed.error().is(kHelperTimeout));

    slow.on_timeout = OnTimeout::Detach;
    slow.args = {"1"};
    const Result<HelperResult> detached = run_helper(slow, 100ms);
    REQUIRE(detached);
    CHECK(detached->detached);
}

TEST_CASE("a missing helper fails NotFound and PATH lookups find executables", "[linux_conformance]") {
    HelperCommand missing;
    missing.program = "reboot-no-such-helper";
    const Result<HelperResult> ran = run_helper(missing);
    REQUIRE_FALSE(ran);
    CHECK(ran.error().kind == ErrorKind::NotFound);
    CHECK(find_in_path("sh", "/nonexistent:/bin:/usr/bin"));
    CHECK_FALSE(find_in_path("reboot-no-such-helper", "/bin:/usr/bin"));
    CHECK_FALSE(find_in_path("sh", "relative/dir"));
}
