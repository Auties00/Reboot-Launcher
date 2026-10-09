// PosixSpawner is built on macOS only.
#if defined(__APPLE__)

#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <optional>
#include <pthread.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <utility>

#include "posix_test_support.hpp"
#include "reboot/posix/ignore_sigpipe.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/posix_spawner.hpp"
#include "reboot/posix/process_start_time.hpp"
#include "unistd.hpp"

using namespace rb;
using namespace rb::posix;
using namespace rb::posix::test;

namespace {

using TimePoint = std::chrono::system_clock::time_point;

const TimePoint kCreated{std::chrono::seconds{1'700'000'000}};

[[nodiscard]] PosixSpawner spawner_reading(std::optional<TimePoint> answer) {
    return PosixSpawner{[answer](u32) -> Result<std::optional<TimePoint>> { return answer; }};
}

[[nodiscard]] ports::ProcessLaunch shell(std::string script, ports::StdioMode stdio) {
    ports::ProcessLaunch launch;
    launch.exe = "/bin/sh";
    launch.args = {"-c", std::move(script)};
    launch.env.vars = {{"PATH", "/usr/bin:/bin"}};
    launch.stdio = stdio;
    return launch;
}

[[nodiscard]] std::string read_to_end(const UniqueFd& fd) {
    std::string out;
    char buffer[4096];
    for (;;) {
        const auto got = ::read(fd.get(), buffer, sizeof buffer);
        if (got < 0 && errno == EINTR) continue;
        REQUIRE(got >= 0);
        if (got == 0) return out;
        out.append(buffer, static_cast<std::size_t>(got));
    }
}

[[nodiscard]] std::string read_line(const UniqueFd& fd) {
    std::string out;
    char c = 0;
    for (;;) {
        const auto got = ::read(fd.get(), &c, 1);
        if (got < 0 && errno == EINTR) continue;
        REQUIRE(got == 1);
        if (c == '\n') return out;
        out.push_back(c);
    }
}

struct Ended {
    std::optional<int> code;
    std::optional<int> signal;
};

[[nodiscard]] Ended reap(u32 pid) {
    int status = 0;
    pid_t reaped = -1;
    while ((reaped = ::waitpid(static_cast<pid_t>(pid), &status, 0)) < 0 && errno == EINTR) {
    }
    REQUIRE(reaped == static_cast<pid_t>(pid));
    if (WIFEXITED(status)) return {.code = WEXITSTATUS(status)};
    if (WIFSIGNALED(status)) return {.signal = WTERMSIG(status)};
    return {};
}

[[nodiscard]] bool reaped_already(u32 pid) { return ::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH; }

[[nodiscard]] bool still_running(u32 pid) {
    int status = 0;
    return ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG) == 0;
}

[[nodiscard]] SpawnedChild spawn_ok(PosixSpawner& spawner, const ports::ProcessLaunch& launch) {
    auto child = spawner.spawn(launch);
    REQUIRE(child.has_value());
    return std::move(*child);
}

}  // namespace

TEST_CASE("spawn passes only launch.env and keeps stdout and stderr apart") {
    PosixSpawner spawner = spawner_reading(kCreated);
    auto launch = shell(R"(printf '%s|%s' "$MARKER" "${HOME-unset}"; printf err >&2)", ports::StdioMode::Capture);
    launch.env.vars.emplace_back("MARKER", "marked");
    SpawnedChild child = spawn_ok(spawner, launch);

    CHECK(child.pid != 0);
    CHECK(child.created == kCreated);
    REQUIRE(child.stdin_write.valid());
    REQUIRE(child.stdout_read.valid());
    REQUIRE(child.stderr_read.valid());
    CHECK((::fcntl(child.stdout_read.get(), F_GETFD) & FD_CLOEXEC) != 0);
    CHECK(read_to_end(child.stdout_read) == "marked|unset");
    CHECK(read_to_end(child.stderr_read) == "err");
    CHECK(reap(child.pid).code == 0);
}

TEST_CASE("stdin carries bytes to the child and its EOF ends a child that waits on it") {
    REQUIRE(ignore_sigpipe().has_value());
    PosixSpawner spawner = spawner_reading(kCreated);
    ports::ProcessLaunch launch;
    launch.exe = "/bin/cat";
    launch.stdio = ports::StdioMode::ControlChannel;
    SpawnedChild child = spawn_ok(spawner, launch);

    const std::string text = "hello\n";
    REQUIRE(::write(child.stdin_write.get(), text.data(), text.size()) == static_cast<ssize_t>(text.size()));
    child.stdin_write.reset();
    CHECK(read_to_end(child.stdout_read) == text);
    CHECK(reap(child.pid).code == 0);
}

TEST_CASE("Null stdio still gives the child a stdin pipe") {
    PosixSpawner spawner = spawner_reading(kCreated);
    SpawnedChild child = spawn_ok(spawner, shell("cat; echo lost; exit 7", ports::StdioMode::Null));
    CHECK_FALSE(child.stdout_read.valid());
    CHECK_FALSE(child.stderr_read.valid());
    REQUIRE(child.stdin_write.valid());
    child.stdin_write.reset();
    CHECK(reap(child.pid).code == 7);
}

TEST_CASE("launch.cwd becomes the child's working directory") {
    const auto scratch = make_scratch("posix-spawn");
    PosixSpawner spawner = spawner_reading(kCreated);
    ports::ProcessLaunch launch;
    launch.exe = "/bin/pwd";
    launch.args = {"-P"};
    launch.cwd = scratch.path();
    launch.stdio = ports::StdioMode::Capture;
    SpawnedChild child = spawn_ok(spawner, launch);

    CHECK(read_to_end(child.stdout_read) == std::filesystem::canonical(scratch.path()).string() + "\n");
    CHECK(reap(child.pid).code == 0);
}

TEST_CASE("the child starts with default signal dispositions and an empty mask") {
    REQUIRE(ignore_sigpipe().has_value());
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGUSR1);
    sigset_t previous;
    REQUIRE(::pthread_sigmask(SIG_BLOCK, &blocked, &previous) == 0);
    PosixSpawner spawner = spawner_reading(kCreated);

    SpawnedChild piped = spawn_ok(spawner, shell("kill -PIPE $$; exit 0", ports::StdioMode::Null));
    SpawnedChild masked = spawn_ok(spawner, shell("kill -USR1 $$; exit 0", ports::StdioMode::Null));
    REQUIRE(::pthread_sigmask(SIG_SETMASK, &previous, nullptr) == 0);

    CHECK(reap(piped.pid).signal == SIGPIPE);
    CHECK(reap(masked.pid).signal == SIGUSR1);
}

TEST_CASE("the child leads its own group unless own_group is false, and spawn_into_group joins one") {
    PosixSpawner spawner = spawner_reading(kCreated);
    ports::ProcessLaunch launch;
    launch.exe = "/bin/sleep";
    launch.args = {"30"};

    SpawnedChild leader = spawn_ok(spawner, launch);
    CHECK(::getpgid(static_cast<pid_t>(leader.pid)) == static_cast<pid_t>(leader.pid));

    auto member = spawner.spawn_into_group(launch, leader.pid);
    REQUIRE(member.has_value());
    CHECK(::getpgid(static_cast<pid_t>(member->pid)) == static_cast<pid_t>(leader.pid));

    launch.own_group = false;
    SpawnedChild ours = spawn_ok(spawner, launch);
    CHECK(::getpgid(static_cast<pid_t>(ours.pid)) == ::getpgrp());

    for (const u32 pid : {leader.pid, member->pid, ours.pid}) ::kill(static_cast<pid_t>(pid), SIGKILL);
    for (const u32 pid : {leader.pid, member->pid, ours.pid}) CHECK(reap(pid).signal == SIGKILL);
}

TEST_CASE("a child whose start time cannot be read is killed and reaped") {
    ports::ProcessLaunch launch;
    launch.exe = "/bin/sleep";
    launch.args = {"30"};

    SECTION("the reader fails") {
        u32 seen = 0;
        PosixSpawner spawner{[&seen](u32 pid) -> Result<std::optional<TimePoint>> {
            seen = pid;
            return std::unexpected(call_failed("proc_pidinfo", EPERM));
        }};
        const auto child = spawner.spawn(launch);
        REQUIRE_FALSE(child.has_value());
        CHECK(child.error().os_error == errno_error(EPERM));
        REQUIRE(seen != 0);
        CHECK(reaped_already(seen));
    }
    SECTION("the reader finds no process") {
        u32 seen = 0;
        PosixSpawner spawner{[&seen](u32 pid) -> Result<std::optional<TimePoint>> {
            seen = pid;
            return std::optional<TimePoint>{};
        }};
        const auto child = spawner.spawn(launch);
        REQUIRE_FALSE(child.has_value());
        CHECK(child.error().os_error == errno_error(ESRCH));
        REQUIRE(seen != 0);
        CHECK(reaped_already(seen));
    }
}

TEST_CASE("spawn of a missing program fails NotFound without reading a start time") {
    bool asked = false;
    PosixSpawner spawner{[&asked](u32) -> Result<std::optional<TimePoint>> {
        asked = true;
        return std::optional<TimePoint>{kCreated};
    }};
    ports::ProcessLaunch launch;
    launch.exe = "/nonexistent/reboot-missing";
    const auto child = spawner.spawn(launch);
    REQUIRE_FALSE(child.has_value());
    CHECK(child.error().kind == ErrorKind::NotFound);
    CHECK_FALSE(asked);
}

TEST_CASE("is_alive holds only for the recorded start time") {
    const auto alive = [](PosixSpawner& spawner, TimePoint recorded) {
        const auto result = spawner.is_alive(4242, recorded);
        REQUIRE(result.has_value());
        return *result;
    };
    PosixSpawner present = spawner_reading(kCreated);
    CHECK(alive(present, kCreated));
    CHECK(alive(present, kCreated + kStartTimeTolerance));
    CHECK_FALSE(alive(present, kCreated + std::chrono::minutes{1}));

    PosixSpawner gone = spawner_reading(std::nullopt);
    CHECK_FALSE(alive(gone, kCreated));

    PosixSpawner failing{[](u32) -> Result<std::optional<TimePoint>> {
        return std::unexpected(call_failed("proc_pidinfo", EPERM));
    }};
    CHECK_FALSE(failing.is_alive(4242, kCreated).has_value());
    CHECK_FALSE(failing.kill_tree(4242, kCreated).has_value());
}

TEST_CASE("kill_tree kills a leader's whole group") {
    PosixSpawner spawner = spawner_reading(kCreated);
    SpawnedChild child = spawn_ok(spawner, shell("sleep 30 & echo $!; wait", ports::StdioMode::Capture));
    const auto grandchild = static_cast<pid_t>(std::stoi(read_line(child.stdout_read)));
    REQUIRE(::getpgid(grandchild) == static_cast<pid_t>(child.pid));

    REQUIRE(spawner.kill_tree(child.pid, child.created).has_value());
    CHECK(reap(child.pid).signal == SIGKILL);
    // The orphaned grandchild is reaped by launchd once the group SIGKILL lands.
    CHECK(wait_for([grandchild] { return reaped_already(static_cast<u32>(grandchild)); }));
}

TEST_CASE("kill_tree spares a reused pid, kills a non-leader alone and accepts a process already gone") {
    PosixSpawner spawner = spawner_reading(kCreated);
    ports::ProcessLaunch launch;
    launch.exe = "/bin/sleep";
    launch.args = {"30"};
    launch.own_group = false;

    SpawnedChild child = spawn_ok(spawner, launch);
    REQUIRE(spawner.kill_tree(child.pid, kCreated + std::chrono::minutes{1}).has_value());
    CHECK(still_running(child.pid));

    // The child shares the test's group, so only the pid itself may be signalled.
    REQUIRE(spawner.kill_tree(child.pid, child.created).has_value());
    CHECK(reap(child.pid).signal == SIGKILL);

    SpawnedChild done = spawn_ok(spawner, shell("exit 0", ports::StdioMode::Null));
    CHECK(reap(done.pid).code == 0);
    CHECK(spawner.kill_tree(done.pid, done.created).has_value());
}

#endif
