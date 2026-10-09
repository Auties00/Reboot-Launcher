#include "tool_run.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "reboot/posix/unique_fd.hpp"

extern char** environ;

namespace reboot::os_linux::ipc {
namespace {

// systemctl show prints a few lines; more than this is not the output asked for.
constexpr std::size_t kMaxOutput = std::size_t{1} << 20;
// The child exits right after its stdout closes, so the reap rarely polls twice.
constexpr int kReapPollMs = 5;

using Clock = std::chrono::steady_clock;

enum class Reap { Exited, Gone, Running };

struct SpawnActions {
    SpawnActions() = default;
    SpawnActions(const SpawnActions&) = delete;
    SpawnActions& operator=(const SpawnActions&) = delete;
    ~SpawnActions() {
        if (ready) ::posix_spawn_file_actions_destroy(&value);
    }

    posix_spawn_file_actions_t value{};
    bool ready = ::posix_spawn_file_actions_init(&value) == 0;
};

struct SpawnAttributes {
    SpawnAttributes() = default;
    SpawnAttributes(const SpawnAttributes&) = delete;
    SpawnAttributes& operator=(const SpawnAttributes&) = delete;
    ~SpawnAttributes() {
        if (ready) ::posix_spawnattr_destroy(&value);
    }

    posix_spawnattr_t value{};
    bool ready = ::posix_spawnattr_init(&value) == 0;
};

[[nodiscard]] int remaining_ms(Clock::time_point until) noexcept {
    const auto left = std::chrono::ceil<std::chrono::milliseconds>(until - Clock::now()).count();
    return static_cast<int>(std::clamp<std::chrono::milliseconds::rep>(left, 0, INT_MAX));
}

[[nodiscard]] std::optional<pid_t> spawn(std::span<const std::string> argv, int stdout_fd) {
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const std::string& argument : argv) args.push_back(const_cast<char*>(argument.c_str()));
    args.push_back(nullptr);

    SpawnActions actions;
    SpawnAttributes attributes;
    if (!actions.ready || !attributes.ready) return std::nullopt;
    sigset_t empty_mask;
    sigset_t every_signal;
    ::sigemptyset(&empty_mask);
    ::sigfillset(&every_signal);
    constexpr auto kFlags = static_cast<short>(POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    const bool configured =
        ::posix_spawn_file_actions_addopen(&actions.value, STDIN_FILENO, "/dev/null", O_RDONLY, 0) == 0 &&
        ::posix_spawn_file_actions_adddup2(&actions.value, stdout_fd, STDOUT_FILENO) == 0 &&
        ::posix_spawn_file_actions_addopen(&actions.value, STDERR_FILENO, "/dev/null", O_WRONLY, 0) == 0 &&
        ::posix_spawnattr_setsigmask(&attributes.value, &empty_mask) == 0 &&
        ::posix_spawnattr_setsigdefault(&attributes.value, &every_signal) == 0 &&
        ::posix_spawnattr_setflags(&attributes.value, kFlags) == 0;
    if (!configured) return std::nullopt;
    pid_t pid = 0;
    if (::posix_spawnp(&pid, args.front(), &actions.value, &attributes.value, args.data(), environ) != 0)
        return std::nullopt;
    return pid;
}

// False on timeout, a read error or too much output; true at end of stream.
[[nodiscard]] bool read_output(int fd, Clock::time_point until, std::string& output) {
    std::array<char, 4096> chunk{};
    for (;;) {
        const int left = remaining_ms(until);
        if (left == 0) return false;
        pollfd entry{.fd = fd, .events = POLLIN, .revents = 0};
        const int ready = ::poll(&entry, 1, left);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) return false;
        const ssize_t got = ::read(fd, chunk.data(), chunk.size());
        if (got < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (got < 0) return false;
        if (got == 0) return true;
        output.append(chunk.data(), static_cast<std::size_t>(got));
        if (output.size() > kMaxOutput) return false;
    }
}

[[nodiscard]] Reap reap(pid_t pid, int& status) noexcept {
    for (;;) {
        const pid_t reaped = ::waitpid(pid, &status, WNOHANG);
        if (reaped == pid) return Reap::Exited;
        if (reaped == 0) return Reap::Running;
        if (errno != EINTR) return Reap::Gone;
    }
}

void kill_and_reap(pid_t pid) noexcept {
    ::kill(pid, SIGKILL);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
}

}  // namespace

std::optional<ToolRun> run_tool(std::span<const std::string> argv, std::chrono::milliseconds deadline) {
    if (argv.empty()) return std::nullopt;
    const Clock::time_point until = Clock::now() + deadline;
    std::array<int, 2> ends{-1, -1};
    if (::pipe2(ends.data(), O_CLOEXEC) != 0) return std::nullopt;
    const posix::UniqueFd read_end{ends[0]};
    posix::UniqueFd write_end{ends[1]};
    // The child's stdio actions would close a write end that took the number 0, 1 or 2.
    if (write_end.get() <= STDERR_FILENO)
        write_end.reset(::fcntl(write_end.get(), F_DUPFD_CLOEXEC, STDERR_FILENO + 1));
    if (!write_end.valid()) return std::nullopt;

    const std::optional<pid_t> pid = spawn(argv, write_end.get());
    write_end.reset();
    if (!pid) return std::nullopt;

    ToolRun run;
    int status = 0;
    Reap state = Reap::Running;
    if (read_output(read_end.get(), until, run.output)) {
        for (;;) {
            state = reap(*pid, status);
            const int left = remaining_ms(until);
            if (state != Reap::Running || left == 0) break;
            ::poll(nullptr, 0, std::min(left, kReapPollMs));
        }
    } else {
        if (reap(*pid, status) == Reap::Running) kill_and_reap(*pid);
        return std::nullopt;
    }
    if (state == Reap::Running) {
        kill_and_reap(*pid);
        return std::nullopt;
    }
    if (state == Reap::Gone || !WIFEXITED(status)) return std::nullopt;
    run.exit_code = WEXITSTATUS(status);
    return run;
}

}  // namespace reboot::os_linux::ipc
