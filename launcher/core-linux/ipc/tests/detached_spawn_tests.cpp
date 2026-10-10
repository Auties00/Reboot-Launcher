#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <fcntl.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "detached_spawn.hpp"
#include "linux_ipc_test_support.hpp"
#include "messages.hpp"
#include "reboot/posix/unique_fd.hpp"

using namespace rb;
using namespace rb::os_linux::ipc;
using namespace rb::os_linux::ipc::test;

namespace {

// A script that reports what it inherited into <dir>/report once it has written everything.
constexpr std::string_view kReporter = R"sh(#!/bin/sh
dir=$(dirname "$0")
{
  echo "args=$*"
  echo "cwd=$(pwd)"
  echo "pid=$$"
  echo "sid=$(cut -d' ' -f6 /proc/$$/stat)"
  echo "ppid=$(cut -d' ' -f4 /proc/$$/stat)"
  echo "stdin=$(readlink /proc/$$/fd/0)"
  echo "stderr=$(readlink /proc/$$/fd/2)"
  echo "leaked= $(ls /proc/$$/fd | tr '\n' ' ')"
  echo "sigign=$(grep SigIgn /proc/$$/status | cut -f2)"
  echo "sigblk=$(grep SigBlk /proc/$$/status | cut -f2)"
  env | sort | sed 's/^/env:/'
} > "$dir/report.tmp"
mv "$dir/report.tmp" "$dir/report"
)sh";

[[nodiscard]] std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) lines.push_back(line);
    return lines;
}

[[nodiscard]] std::string value_of(const std::vector<std::string>& lines, std::string_view key) {
    for (const std::string& line : lines)
        if (line.starts_with(key) && line.size() > key.size() && line[key.size()] == '=')
            return line.substr(key.size() + 1);
    FAIL("no " << key << " in the report");
    return {};
}

[[nodiscard]] std::uint64_t hex_mask(const std::string& text) {
    std::uint64_t mask = 0;
    REQUIRE(std::from_chars(text.data(), text.data() + text.size(), mask, 16).ec == std::errc{});
    return mask;
}

[[nodiscard]] std::uint64_t signal_bit(int number) { return std::uint64_t{1} << (number - 1); }

// Restores SIGUSR2's disposition and the mask on destruction.
class SignalState {
public:
    SignalState() {
        REQUIRE(::sigaction(SIGUSR2, nullptr, &action_) == 0);
        REQUIRE(::pthread_sigmask(SIG_SETMASK, nullptr, &mask_) == 0);
    }
    ~SignalState() {
        ::sigaction(SIGUSR2, &action_, nullptr);
        ::pthread_sigmask(SIG_SETMASK, &mask_, nullptr);
    }
    SignalState(const SignalState&) = delete;
    SignalState& operator=(const SignalState&) = delete;

private:
    struct sigaction action_ {};
    sigset_t mask_{};
};

}  // namespace

TEST_CASE("the detached program gets only its argv, envp, cwd and /dev/null stdio", "[detached_spawn]") {
    const auto scratch = make_private_scratch("linux-spawn");
    const NativePath script = scratch.path() / "reporter";
    write_text(script, kReporter, 0700);
    const NativePath cwd = scratch.path() / "cwd";
    REQUIRE(::mkdir(cwd.c_str(), 0700) == 0);

    const SignalState restore;
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    REQUIRE(::sigaction(SIGUSR2, &ignore, nullptr) == 0);
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGUSR1);
    REQUIRE(::pthread_sigmask(SIG_BLOCK, &blocked, nullptr) == 0);
    // An fd without close-on-exec, as a host app may hold, numbered above the reporting shell's own
    // (its command substitution pipe takes the lowest free number).
    const posix::UniqueFd opened{::open("/dev/null", O_RDONLY | O_CLOEXEC)};
    REQUIRE(opened.valid());
    const posix::UniqueFd leaky{::fcntl(opened.get(), F_DUPFD, 100)};
    REQUIRE(leaky.valid());
    REQUIRE((::fcntl(leaky.get(), F_GETFD) & FD_CLOEXEC) == 0);

    const auto spawned = spawn_detached({
        .program = script,
        .argv = {script.string(), "run", "--origin=on-demand"},
        .envp = {"PATH=/usr/local/bin:/usr/bin:/bin", "REBOOT_MARKER=present"},
        .cwd = cwd,
    });
    REQUIRE(spawned);
    REQUIRE(wait_for([&] { return path_exists(scratch.path() / "report"); }));
    const std::vector<std::string> report = lines_of(read_text(scratch.path() / "report").value_or(""));
    INFO(read_text(scratch.path() / "report").value_or(""));

    CHECK(value_of(report, "args") == "run --origin=on-demand");
    CHECK(value_of(report, "cwd") == cwd.string());
    // In the session the intermediate child created, which it does not lead, so it never gains a terminal.
    CHECK(value_of(report, "sid") != std::to_string(::getsid(0)));
    CHECK(value_of(report, "sid") != value_of(report, "pid"));
    CHECK(value_of(report, "ppid") != std::to_string(::getpid()));
    CHECK(value_of(report, "stdin") == "/dev/null");
    CHECK(value_of(report, "stderr") == "/dev/null");
    CHECK(value_of(report, "leaked").find(" " + std::to_string(leaky.get()) + " ") == std::string::npos);
    CHECK((hex_mask(value_of(report, "sigign")) & signal_bit(SIGUSR2)) == 0);
    CHECK((hex_mask(value_of(report, "sigblk")) & signal_bit(SIGUSR1)) == 0);
    CHECK(value_of(report, "env:REBOOT_MARKER") == "present");
    // The shell adds only its own bookkeeping variables.
    const auto expected_variable = [](std::string_view line) {
        for (const std::string_view name : {"PATH=", "REBOOT_MARKER=", "PWD=", "OLDPWD=", "SHLVL=", "_="})
            if (line.substr(4).starts_with(name)) return true;
        return false;
    };
    for (const std::string& line : report)
        if (line.starts_with("env:")) CHECK(expected_variable(line));
}

TEST_CASE("an exec failure comes back as platform.ipc_engine_spawn_failed with its errno", "[detached_spawn]") {
    const auto scratch = make_private_scratch("linux-spawn");
    const NativePath missing = scratch.path() / "reboot-engine";
    const auto spawned =
        spawn_detached({.program = missing, .argv = {missing.string()}, .envp = {}, .cwd = scratch.path()});
    REQUIRE_FALSE(spawned);
    CHECK(spawned.error().is(kEngineSpawnFailed));
    REQUIRE(spawned.error().os_error);
    CHECK(spawned.error().os_error->code == ENOENT);

    const NativePath script = scratch.path() / "not-executable";
    write_text(script, "#!/bin/sh\n", 0600);
    const auto denied =
        spawn_detached({.program = script, .argv = {script.string()}, .envp = {}, .cwd = scratch.path()});
    REQUIRE_FALSE(denied);
    REQUIRE(denied.error().os_error);
    CHECK(denied.error().os_error->code == EACCES);
}

TEST_CASE("a working directory that cannot be entered fails the spawn", "[detached_spawn]") {
    const auto scratch = make_private_scratch("linux-spawn");
    const auto spawned = spawn_detached(
        {.program = "/bin/sh", .argv = {"/bin/sh", "-c", "true"}, .envp = {}, .cwd = scratch.path() / "missing"});
    REQUIRE_FALSE(spawned);
    CHECK(spawned.error().is(kEngineSpawnFailed));
    REQUIRE(spawned.error().os_error);
    CHECK(spawned.error().os_error->code == ENOENT);
}
