#include "helper_process.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "child_reaping.hpp"
#include "linux_syscalls.hpp"
#include "messages.hpp"
#include "process_environment.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace reboot::os_linux::platform {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaxOutput = std::size_t{1} << 20;
// Without a pidfd (a kernel before 5.3) the exit is polled this often.
constexpr int kExitPollMs = 10;

class SpawnActions {
public:
    SpawnActions() noexcept { ready_ = ::posix_spawn_file_actions_init(&value_) == 0; }
    ~SpawnActions() {
        if (ready_) ::posix_spawn_file_actions_destroy(&value_);
    }
    SpawnActions(const SpawnActions&) = delete;
    SpawnActions& operator=(const SpawnActions&) = delete;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] posix_spawn_file_actions_t* get() noexcept { return &value_; }

private:
    posix_spawn_file_actions_t value_{};
    bool ready_ = false;
};

class SpawnAttributes {
public:
    SpawnAttributes() noexcept { ready_ = ::posix_spawnattr_init(&value_) == 0; }
    ~SpawnAttributes() {
        if (ready_) ::posix_spawnattr_destroy(&value_);
    }
    SpawnAttributes(const SpawnAttributes&) = delete;
    SpawnAttributes& operator=(const SpawnAttributes&) = delete;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] posix_spawnattr_t* get() noexcept { return &value_; }

private:
    posix_spawnattr_t value_{};
    bool ready_ = false;
};

[[nodiscard]] int remaining_ms(Clock::time_point until) noexcept {
    const auto left = std::chrono::ceil<std::chrono::milliseconds>(until - Clock::now()).count();
    return static_cast<int>(std::clamp<std::chrono::milliseconds::rep>(left, 0, INT_MAX));
}

[[nodiscard]] std::vector<char*> pointers(std::vector<std::string>& strings) {
    std::vector<char*> out;
    out.reserve(strings.size() + 1);
    for (std::string& text : strings) out.push_back(text.data());
    out.push_back(nullptr);
    return out;
}

// Blocks until the listed child `pid` has been reaped.
[[nodiscard]] WaitResult wait_for_exit(u32 pid, int pidfd) {
    for (;;) {
        WaitResult waited = reap_listed_child(pid);
        if (waited.outcome != WaitOutcome::Running) return waited;
        pollfd entry{.fd = pidfd, .events = POLLIN, .revents = 0};
        if (pidfd >= 0)
            (void)::poll(&entry, 1, -1);
        else
            (void)::poll(nullptr, 0, kExitPollMs);
    }
}

[[nodiscard]] std::optional<int> exit_code_of(const WaitResult& waited) noexcept {
    if (waited.outcome != WaitOutcome::Ended || waited.status.code != CLD_EXITED) return std::nullopt;
    return waited.status.status;
}

[[nodiscard]] Result<pid_t> spawn(const HelperCommand& command, int stdout_fd) {
    std::vector<std::string> argv_storage;
    argv_storage.reserve(command.args.size() + 1);
    argv_storage.push_back(command.program);
    argv_storage.insert(argv_storage.end(), command.args.begin(), command.args.end());
    std::vector<char*> argv = pointers(argv_storage);

    std::vector<std::string> env_storage = envp_strings(command.env ? *command.env : current_environment());
    std::vector<char*> envp = pointers(env_storage);

    SpawnActions actions;
    SpawnAttributes attributes;
    if (!actions.ready() || !attributes.ready())
        return std::unexpected(posix::call_failed("posix_spawn", ENOMEM, NativePath{command.program}));
    sigset_t empty_mask;
    sigset_t every_signal;
    ::sigemptyset(&empty_mask);
    ::sigfillset(&every_signal);
    constexpr auto kFlags = static_cast<short>(POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    int setup = ::posix_spawn_file_actions_addopen(actions.get(), STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (setup == 0) {
        setup = stdout_fd >= 0
                    ? ::posix_spawn_file_actions_adddup2(actions.get(), stdout_fd, STDOUT_FILENO)
                    : ::posix_spawn_file_actions_addopen(actions.get(), STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    }
    if (setup == 0)
        setup = ::posix_spawn_file_actions_addopen(actions.get(), STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (setup == 0) setup = ::posix_spawnattr_setsigmask(attributes.get(), &empty_mask);
    if (setup == 0) setup = ::posix_spawnattr_setsigdefault(attributes.get(), &every_signal);
    if (setup == 0) setup = ::posix_spawnattr_setflags(attributes.get(), kFlags);
    if (setup != 0) return std::unexpected(posix::call_failed("posix_spawn", setup, NativePath{command.program}));

    pid_t pid = 0;
    const auto held = ChildTable::lock();
    if (const int error =
            ::posix_spawnp(&pid, command.program.c_str(), actions.get(), attributes.get(), argv.data(), envp.data());
        error != 0)
        return std::unexpected(posix::call_failed("posix_spawn", error, NativePath{command.program}));
    ChildTable::add(static_cast<u32>(pid));
    return pid;
}

}  // namespace

Result<HelperResult> run_helper(const HelperCommand& command, std::chrono::milliseconds deadline) {
    const Clock::time_point until = Clock::now() + deadline;
    posix::UniqueFd read_end;
    posix::UniqueFd write_end;
    if (command.capture_stdout) {
        std::array<int, 2> ends{-1, -1};
        if (::pipe2(ends.data(), O_CLOEXEC) != 0) return std::unexpected(posix::call_failed("pipe2", errno));
        read_end.reset(ends[0]);
        write_end.reset(ends[1]);
        // The stdin and stderr actions would close a write end numbered 0 to 2.
        if (write_end.get() <= STDERR_FILENO) write_end.reset(::fcntl(write_end.get(), F_DUPFD_CLOEXEC, STDERR_FILENO + 1));
        if (!write_end.valid()) return std::unexpected(posix::call_failed("fcntl", errno));
    }

    const Result<pid_t> spawned = spawn(command, write_end.get());
    write_end.reset();
    if (!spawned) return std::unexpected(spawned.error());
    const auto pid = static_cast<u32>(*spawned);
    posix::UniqueFd pidfd{sys_pidfd_open(*spawned)};

    HelperResult result;
    std::optional<WaitResult> ended;
    std::array<char, 4096> chunk{};
    while (!ended || read_end.valid()) {
        const int left = remaining_ms(until);
        if (left == 0) break;
        std::array<pollfd, 2> fds{};
        nfds_t count = 0;
        if (!ended && pidfd.valid()) fds[count++] = {.fd = pidfd.get(), .events = POLLIN, .revents = 0};
        if (read_end.valid()) fds[count++] = {.fd = read_end.get(), .events = POLLIN, .revents = 0};
        const bool polling_exit = !ended && !pidfd.valid();
        const int ready = ::poll(fds.data(), count, polling_exit ? std::min(left, kExitPollMs) : left);
        if (ready < 0 && errno != EINTR) {
            const int error = errno;
            if (!ended) {
                ::kill(*spawned, SIGKILL);
                (void)wait_for_exit(pid, pidfd.get());
            }
            return std::unexpected(posix::call_failed("poll", error));
        }
        if (read_end.valid() && ready > 0 && (fds[count - 1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            const ssize_t got = ::read(read_end.get(), chunk.data(), chunk.size());
            if (got == 0 || (got < 0 && errno != EINTR && errno != EAGAIN)) {
                read_end.reset();
            } else if (got > 0 && result.output.size() < kMaxOutput) {
                result.output.append(chunk.data(), static_cast<std::size_t>(got));
            }
        }
        if (!ended) {
            WaitResult waited = reap_listed_child(pid);
            if (waited.outcome != WaitOutcome::Running) ended = waited;
        }
    }

    if (ended) {
        result.exit_code = exit_code_of(*ended);
        return result;
    }
    if (command.on_timeout == OnTimeout::Detach) {
        std::thread([pid, fd = pidfd.release()] {
            const posix::UniqueFd owned{fd};
            (void)wait_for_exit(pid, owned.get());
        }).detach();
        result.detached = true;
        return result;
    }
    ::kill(*spawned, SIGKILL);
    (void)wait_for_exit(pid, pidfd.get());
    return make_diag(ErrorDomain::Platform, kHelperTimeout)
        .arg("program", command.program)
        .arg("deadline", deadline)
        .retryable()
        .fail();
}

std::optional<NativePath> find_in_path(std::string_view name, std::string_view path_list) {
    if (name.empty() || name.find('/') != std::string_view::npos) return std::nullopt;
    while (true) {
        const std::size_t colon = path_list.find(':');
        const std::string_view dir = path_list.substr(0, colon);
        if (dir.starts_with('/')) {
            const NativePath candidate = NativePath{dir} / name;
            struct stat info {};
            if (::stat(candidate.c_str(), &info) == 0 && S_ISREG(info.st_mode) && ::access(candidate.c_str(), X_OK) == 0)
                return candidate;
        }
        if (colon == std::string_view::npos) return std::nullopt;
        path_list.remove_prefix(colon + 1);
    }
}

Diagnostic helper_failed(std::string_view program, const HelperResult& result) {
    return make_diag(ErrorDomain::Platform, kHelperFailed)
        .arg("program", program)
        .arg("exit_code", static_cast<i64>(result.exit_code.value_or(-1)));
}

}  // namespace reboot::os_linux::platform
