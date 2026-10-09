#include "darwin.hpp"

#include "run_program.hpp"

#include <signal.h>
#include <sys/event.h>
#include <sys/wait.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <ctime>
#include <optional>
#include <utility>

#include "kevent.hpp"
#include "messages.hpp"
#include "proc_info.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/posix_spawner.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "wait_status.hpp"

namespace reboot::os_macos::platform {

namespace {

using Clock = std::chrono::steady_clock;

// Output a grandchild keeps open is not waited for past the exit.
constexpr std::chrono::milliseconds kOutputDrainBound{2000};
constexpr std::size_t kReadChunk = 16 * 1024;

[[nodiscard]] int reap(pid_t pid) noexcept {
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return 0;
    }
    return status;
}

[[nodiscard]] timespec to_timespec(Clock::duration left) noexcept {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(left);
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(left - seconds);
    return timespec{static_cast<time_t>(seconds.count()), static_cast<long>(nanos.count())};
}

}  // namespace

Result<ProgramResult> run_program(const NativePath& program, std::vector<std::string> args,
                                  std::chrono::milliseconds deadline) {
    posix::PosixSpawner spawner{process_start_time};
    ports::ProcessLaunch launch;
    launch.exe = program;
    launch.args = std::move(args);
    launch.env.vars.emplace_back("PATH", "/usr/bin:/bin:/usr/sbin:/sbin");
    launch.cwd = "/";
    launch.stdio = ports::StdioMode::Capture;
    launch.own_group = true;
    Result<posix::SpawnedChild> spawned = spawner.spawn(launch);
    if (!spawned) return std::unexpected(std::move(spawned.error()));
    spawned->stdin_write.reset();
    const auto pid = static_cast<pid_t>(spawned->pid);
    std::array<posix::UniqueFd, 2> outputs{std::move(spawned->stdout_read), std::move(spawned->stderr_read)};
    std::array<std::string, 2> captured;

    const auto abandon = [&](Diagnostic failure) -> Result<ProgramResult> {
        ::kill(-pid, SIGKILL);
        (void)reap(pid);
        return std::unexpected(std::move(failure));
    };

    posix::UniqueFd queue{::kqueue()};
    if (!queue.valid()) return abandon(posix::call_failed("kqueue", errno));
    const std::array<struct kevent, 2> reads{
        make_kevent(static_cast<std::uintptr_t>(outputs[0].get()), EVFILT_READ, EV_ADD, 0, 0, nullptr),
        make_kevent(static_cast<std::uintptr_t>(outputs[1].get()), EVFILT_READ, EV_ADD, 0, 0, &captured[1])};
    if (::kevent(queue.get(), reads.data(), static_cast<int>(reads.size()), nullptr, 0, nullptr) != 0)
        return abandon(posix::call_failed("kevent", errno));
    bool exited = false;
    const struct kevent exit_watch =
        make_kevent(static_cast<std::uintptr_t>(pid), EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
    if (::kevent(queue.get(), &exit_watch, 1, nullptr, 0, nullptr) != 0) {
        if (errno != ESRCH) return abandon(posix::call_failed("kevent", errno));
        exited = true;
    }
    // An exit before the watch was armed is never reported.
    exited = exited || has_exited(spawned->pid);

    const Clock::time_point give_up = Clock::now() + deadline;
    std::optional<Clock::time_point> drain_until;
    if (exited) drain_until = Clock::now() + kOutputDrainBound;
    std::array<char, kReadChunk> buffer{};
    while (outputs[0].valid() || outputs[1].valid()) {
        const Clock::time_point now = Clock::now();
        if (now >= give_up)
            return abandon(make_diag(ErrorDomain::Platform, kHelperTimeout)
                               .arg("program", program)
                               .arg("deadline", deadline)
                               .retryable()
                               .build());
        if (drain_until && now >= *drain_until) break;
        const timespec wait = to_timespec((drain_until ? std::min(give_up, *drain_until) : give_up) - now);
        std::array<struct kevent, 4> events{};
        const int count = ::kevent(queue.get(), nullptr, 0, events.data(), static_cast<int>(events.size()), &wait);
        if (count < 0) {
            if (errno == EINTR) continue;
            return abandon(posix::call_failed("kevent", errno));
        }
        for (int i = 0; i < count; ++i) {
            const struct kevent& event = events[static_cast<std::size_t>(i)];
            if (event.filter == EVFILT_PROC) {
                if (!exited) drain_until = Clock::now() + kOutputDrainBound;
                exited = true;
                continue;
            }
            const std::size_t stream = event.udata == nullptr ? 0 : 1;
            if (!outputs[stream].valid()) continue;
            const ssize_t got = ::read(outputs[stream].get(), buffer.data(), buffer.size());
            if (got > 0) {
                captured[stream].append(buffer.data(), static_cast<std::size_t>(got));
            } else if (got == 0 || errno != EINTR) {
                // Closing the descriptor also drops its kevent.
                outputs[stream].reset();
            }
        }
    }
    if (!exited) {
        // Both pipes closed; the exit itself is at most a deadline away.
        while (Clock::now() < give_up) {
            const timespec wait = to_timespec(give_up - Clock::now());
            struct kevent event {};
            const int count = ::kevent(queue.get(), nullptr, 0, &event, 1, &wait);
            if (count < 0 && errno == EINTR) continue;
            if (count != 0) break;
        }
        if (Clock::now() >= give_up)
            return abandon(make_diag(ErrorDomain::Platform, kHelperTimeout)
                               .arg("program", program)
                               .arg("deadline", deadline)
                               .retryable()
                               .build());
    }
    const ports::ChildExit exit = decode_wait_status(reap(pid));
    return ProgramResult{.code = exit.code, .signal = exit.signal, .output = captured[0] + captured[1]};
}

}  // namespace reboot::os_macos::platform
