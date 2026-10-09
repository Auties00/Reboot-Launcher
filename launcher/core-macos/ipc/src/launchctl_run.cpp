#include "launchctl_run.hpp"

#include "unistd.hpp"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/event.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string_view>
#include <vector>

#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace reboot::os_macos::ipc {
namespace {

constexpr const char* kLaunchctl = "/bin/launchctl";

using Clock = std::chrono::steady_clock;

class SpawnFileActions {
public:
    SpawnFileActions() noexcept : error_(::posix_spawn_file_actions_init(&actions_)) {}
    ~SpawnFileActions() {
        if (error_ == 0) ::posix_spawn_file_actions_destroy(&actions_);
    }
    SpawnFileActions(const SpawnFileActions&) = delete;
    SpawnFileActions& operator=(const SpawnFileActions&) = delete;

    [[nodiscard]] int error() const noexcept { return error_; }
    [[nodiscard]] posix_spawn_file_actions_t* get() noexcept { return &actions_; }

private:
    posix_spawn_file_actions_t actions_{};
    int error_ = 0;
};

class SpawnAttributes {
public:
    SpawnAttributes() noexcept : error_(::posix_spawnattr_init(&attributes_)) {}
    ~SpawnAttributes() {
        if (error_ == 0) ::posix_spawnattr_destroy(&attributes_);
    }
    SpawnAttributes(const SpawnAttributes&) = delete;
    SpawnAttributes& operator=(const SpawnAttributes&) = delete;

    [[nodiscard]] int error() const noexcept { return error_; }
    [[nodiscard]] posix_spawnattr_t* get() noexcept { return &attributes_; }

private:
    posix_spawnattr_t attributes_{};
    int error_ = 0;
};

[[nodiscard]] Diagnostic launchctl_failed(std::string_view call, int error) {
    return posix::call_failed(call, error, NativePath{kLaunchctl});
}

// The first non-zero result of the setup calls.
[[nodiscard]] int configure(SpawnFileActions& actions, SpawnAttributes& attributes) {
    if (actions.error() != 0) return actions.error();
    if (attributes.error() != 0) return attributes.error();
    sigset_t empty_mask;
    sigset_t every_signal;
    ::sigemptyset(&empty_mask);
    ::sigfillset(&every_signal);
    constexpr int kFlags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_CLOEXEC_DEFAULT |
                           POSIX_SPAWN_START_SUSPENDED;
    for (const int result : {
             ::posix_spawn_file_actions_addopen(actions.get(), STDIN_FILENO, "/dev/null", O_RDONLY, 0),
             ::posix_spawn_file_actions_addopen(actions.get(), STDOUT_FILENO, "/dev/null", O_WRONLY, 0),
             ::posix_spawn_file_actions_addopen(actions.get(), STDERR_FILENO, "/dev/null", O_WRONLY, 0),
             ::posix_spawnattr_setsigmask(attributes.get(), &empty_mask),
             ::posix_spawnattr_setsigdefault(attributes.get(), &every_signal),
             ::posix_spawnattr_setflags(attributes.get(), static_cast<short>(kFlags)),
         }) {
        if (result != 0) return result;
    }
    return 0;
}

[[nodiscard]] timespec timeout_until(Clock::time_point until) {
    const Clock::duration left = std::max(until - Clock::now(), Clock::duration::zero());
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(left);
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(left - seconds);
    timespec timeout{};
    timeout.tv_sec = static_cast<time_t>(seconds.count());
    timeout.tv_nsec = static_cast<long>(nanoseconds.count());
    return timeout;
}

// The wait status of the watched child; nullopt when `until` passes first.
[[nodiscard]] Result<std::optional<int>> await_exit(int queue, std::optional<Clock::time_point> until) {
    for (;;) {
        timespec timeout{};
        if (until) timeout = timeout_until(*until);
        struct kevent event {};
        const int got = ::kevent(queue, nullptr, 0, &event, 1, until ? &timeout : nullptr);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) return std::unexpected(launchctl_failed("kevent", errno));
        if (got == 0) return std::optional<int>{};
        if ((event.flags & EV_ERROR) != 0) return std::unexpected(launchctl_failed("kevent", static_cast<int>(event.data)));
        if ((event.fflags & NOTE_EXIT) != 0) return std::optional<int>{static_cast<int>(event.data)};
    }
}

// Collects the zombie, unless the kernel does so itself for a host that ignores SIGCHLD.
void reap(pid_t pid) noexcept {
    struct sigaction current {};
    if (::sigaction(SIGCHLD, nullptr, &current) == 0 &&
        (current.sa_handler == SIG_IGN || (current.sa_flags & SA_NOCLDWAIT) != 0))
        return;
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
}

void kill_and_reap(pid_t pid) noexcept {
    ::kill(pid, SIGKILL);
    reap(pid);
}

}  // namespace

Result<LaunchctlRun> run_launchctl(std::span<const std::string> arguments, std::chrono::milliseconds deadline) {
    const Clock::time_point until = Clock::now() + deadline;
    // A kqueue is never inherited by a child.
    const posix::UniqueFd queue{::kqueue()};
    if (!queue.valid()) return std::unexpected(launchctl_failed("kqueue", errno));

    std::vector<std::string> owned_arguments{kLaunchctl};
    owned_arguments.insert(owned_arguments.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.reserve(owned_arguments.size() + 1);
    for (std::string& argument : owned_arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    std::array<char*, 1> envp{nullptr};

    SpawnFileActions actions;
    SpawnAttributes attributes;
    if (const int error = configure(actions, attributes); error != 0)
        return std::unexpected(launchctl_failed("posix_spawn", error));
    pid_t pid = 0;
    if (const int error = ::posix_spawn(&pid, kLaunchctl, actions.get(), attributes.get(), argv.data(), envp.data());
        error != 0)
        return std::unexpected(launchctl_failed("posix_spawn", error));

    struct kevent watch {};
    watch.ident = static_cast<std::uintptr_t>(pid);
    watch.filter = EVFILT_PROC;
    watch.flags = EV_ADD;
    watch.fflags = NOTE_EXIT | NOTE_EXITSTATUS;
    if (::kevent(queue.get(), &watch, 1, nullptr, 0, nullptr) != 0) {
        const int error = errno;
        kill_and_reap(pid);
        return std::unexpected(launchctl_failed("kevent", error));
    }
    if (::kill(pid, SIGCONT) != 0) {
        const int error = errno;
        kill_and_reap(pid);
        return std::unexpected(launchctl_failed("kill", error));
    }

    const Result<std::optional<int>> exited = await_exit(queue.get(), until);
    if (!exited) {
        kill_and_reap(pid);
        return std::unexpected(exited.error());
    }
    if (!*exited) {
        ::kill(pid, SIGKILL);
        // The kill is certain to end it, so this wait has no deadline.
        (void)await_exit(queue.get(), std::nullopt);
        reap(pid);
        return LaunchctlRun{.end = LaunchctlRun::End::TimedOut, .code = 0};
    }
    reap(pid);
    int status = **exited;
    if (WIFEXITED(status)) return LaunchctlRun{.end = LaunchctlRun::End::Exited, .code = WEXITSTATUS(status)};
    return LaunchctlRun{.end = LaunchctlRun::End::Signalled, .code = WTERMSIG(status)};
}

}  // namespace reboot::os_macos::ipc
