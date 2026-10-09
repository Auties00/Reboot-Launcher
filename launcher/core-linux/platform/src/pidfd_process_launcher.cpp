#include "reboot/os_linux/platform/pidfd_process_launcher.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <deque>
#include <exception>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <span>
#include <string>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "child_reaping.hpp"
#include "helper_process.hpp"
#include "linux_syscalls.hpp"
#include "process_environment.hpp"
#include "proc_stat.hpp"
#include "systemd_user.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/process_start_time.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "text_files.hpp"

namespace rb::os_linux::platform {

namespace {

using SystemTime = std::chrono::system_clock::time_point;
using Bytes = std::span<const u8>;
using OutputCallback = UniqueFunction<void(Bytes)>;

constexpr u64 kClonePidfd = 0x00001000;
constexpr std::size_t kReadChunk = 64 * 1024;
// Output nobody listens to yet is kept up to this, then the pipe is left to fill.
constexpr std::size_t kMaxPending = 4 * 1024 * 1024;
// Reads per readiness event, so one chatty child cannot starve the others.
constexpr int kReadsPerEvent = 16;
// Reads after the exit, bounded since descendants may keep writing to the inherited pipes.
constexpr int kDrainReads = 64;
constexpr int kMaxEvents = 64;
constexpr int kExecFailedStatus = 127;

// struct clone_args as of Linux 5.3; glibc 2.28's headers predate it.
struct CloneArgs {
    u64 flags = 0;
    u64 pidfd = 0;
    u64 child_tid = 0;
    u64 parent_tid = 0;
    u64 exit_signal = 0;
    u64 stack = 0;
    u64 stack_size = 0;
    u64 tls = 0;
};

enum class Source : u8 { Wake, ChildSignal, Exit, Stdout, Stderr, Stdin };

[[nodiscard]] u64 tag(u64 id, Source source) noexcept { return (id << 8) | static_cast<u64>(source); }

[[nodiscard]] bool is_missing(const Diagnostic& error) noexcept {
    return error.os_error && (error.os_error->code == ENOENT || error.os_error->code == ESRCH);
}

// ---------------------------------------------------------------------------------------------
// Children and the epoll thread.

struct ChildState {
    u64 id = 0;
    u32 pid = 0;
    SystemTime created;
    bool own_group = true;
    std::optional<std::string> scope_unit;

    std::mutex mutex;
    std::condition_variable idle;
    // Guarded by mutex. pidfd and stdin_fd are closed only on the epoll thread.
    posix::UniqueFd pidfd;
    posix::UniqueFd stdin_fd;
    std::vector<u8> stdin_queue;
    bool stdin_watch_requested = false;
    bool stdin_close_requested = false;
    // Ended but kept a zombie until the handle goes, so the group id stays ours for terminate_tree.
    bool exited = false;
    bool reaped = false;
    bool detached = false;
    bool exit_delivered = false;
    int delivering = 0;
    OutputCallback on_stdout;
    OutputCallback on_stderr;
    UniqueFunction<void(ports::ChildExit)> on_exit;
    std::vector<u8> pending_stdout;
    std::vector<u8> pending_stderr;
    std::optional<ports::ChildExit> pending_exit;

    // Epoll thread only.
    posix::UniqueFd stdout_fd;
    posix::UniqueFd stderr_fd;
    bool stdout_paused = false;
    bool stderr_paused = false;
    bool stdin_watched = false;
};

struct LoopCore;
using Command = UniqueFunction<void(LoopCore&)>;

struct LoopCore {
    posix::UniqueFd epoll;
    posix::UniqueFd wake;
    // Only as PID 1: written by the SIGCHLD handler.
    posix::UniqueFd child_signal_read;
    posix::UniqueFd child_signal_write;
    std::thread::id thread;

    std::mutex mutex;
    std::vector<Command> commands;
    bool stopping = false;

    // Epoll thread only.
    std::unordered_map<u64, std::shared_ptr<ChildState>> children;
    std::vector<u8> read_buffer = std::vector<u8>(kReadChunk);

    void post(Command command) {
        {
            const std::lock_guard lock(mutex);
            commands.push_back(std::move(command));
        }
        const u64 one = 1;
        while (::write(wake.get(), &one, sizeof one) < 0 && errno == EINTR) {
        }
    }

    void watch(int fd, u64 id, Source source, u32 events) {
        epoll_event event{};
        event.events = events;
        event.data.u64 = tag(id, source);
        if (::epoll_ctl(epoll.get(), EPOLL_CTL_ADD, fd, &event) != 0)
            REBOOT_LOG_ERROR(Engine, "epoll_ctl ADD failed with errno {} for child {}", errno, id);
    }

    void unwatch(int fd) {
        if (fd >= 0) ::epoll_ctl(epoll.get(), EPOLL_CTL_DEL, fd, nullptr);
    }
};

std::atomic<int> g_child_signal_fd{-1};

void on_child_signal(int) {
    const int saved = errno;
    const int fd = g_child_signal_fd.load(std::memory_order_relaxed);
    const char byte = 0;
    if (fd >= 0) (void)::write(fd, &byte, 1);
    errno = saved;
}

[[nodiscard]] bool on_loop_thread(const LoopCore& core) noexcept { return std::this_thread::get_id() == core.thread; }

// Runs a callback with the state unlocked; it goes back in its slot unless replaced or detached.
template <class Callback, class... Args>
void invoke(ChildState& state, std::unique_lock<std::mutex>& lock, Callback ChildState::*slot, bool keep, Args&&... args) {
    Callback local = std::move(state.*slot);
    ++state.delivering;
    lock.unlock();
    try {
        local(std::forward<Args>(args)...);
    } catch (const std::exception& error) {
        REBOOT_LOG_ERROR(Engine, "internal.bug: a child process callback threw: {}", error.what());
    } catch (...) {
        REBOOT_LOG_ERROR(Engine, "internal.bug: a child process callback threw");
    }
    lock.lock();
    --state.delivering;
    if (keep && !state.detached && !(state.*slot)) state.*slot = std::move(local);
    state.idle.notify_all();
}

void flush_output(ChildState& state, std::unique_lock<std::mutex>& lock, Source which) {
    OutputCallback ChildState::*slot = which == Source::Stdout ? &ChildState::on_stdout : &ChildState::on_stderr;
    std::vector<u8>& pending = which == Source::Stdout ? state.pending_stdout : state.pending_stderr;
    while (!state.detached && state.*slot && !pending.empty()) {
        const std::vector<u8> bytes = std::exchange(pending, {});
        invoke(state, lock, slot, true, Bytes{bytes});
    }
}

void flush_exit(ChildState& state, std::unique_lock<std::mutex>& lock) {
    if (state.detached || state.exit_delivered || !state.pending_exit || !state.on_exit) return;
    flush_output(state, lock, Source::Stdout);
    flush_output(state, lock, Source::Stderr);
    if (state.detached || state.exit_delivered || !state.on_exit) return;
    state.exit_delivered = true;
    const ports::ChildExit exit = *state.pending_exit;
    invoke(state, lock, &ChildState::on_exit, false, exit);
}

// Resumes reading a pipe that was paused while nobody listened.
void resume_output(LoopCore& core, ChildState& state, Source which) {
    bool& paused = which == Source::Stdout ? state.stdout_paused : state.stderr_paused;
    const posix::UniqueFd& fd = which == Source::Stdout ? state.stdout_fd : state.stderr_fd;
    if (!paused || !fd.valid()) return;
    paused = false;
    core.watch(fd.get(), state.id, which, EPOLLIN);
}

void close_output(LoopCore& core, ChildState& state, Source which) {
    posix::UniqueFd& fd = which == Source::Stdout ? state.stdout_fd : state.stderr_fd;
    bool& paused = which == Source::Stdout ? state.stdout_paused : state.stderr_paused;
    if (!paused) core.unwatch(fd.get());
    fd.reset();
}

void close_stdin_now(LoopCore& core, ChildState& state) {
    if (state.stdin_watched) core.unwatch(state.stdin_fd.get());
    state.stdin_watched = false;
    state.stdin_fd.reset();
    state.stdin_queue.clear();
}

// With `to_end`, reads until the pipe is empty, as after the child exited.
void read_output(LoopCore& core, ChildState& state, Source which, bool to_end) {
    posix::UniqueFd& fd = which == Source::Stdout ? state.stdout_fd : state.stderr_fd;
    for (int round = 0; fd.valid() && round < (to_end ? kDrainReads : kReadsPerEvent); ++round) {
        const ssize_t got = ::read(fd.get(), core.read_buffer.data(), core.read_buffer.size());
        if (got < 0 && errno == EINTR) continue;
        if (got < 0 && errno == EAGAIN) return;
        if (got <= 0) {
            close_output(core, state, which);
            return;
        }
        std::unique_lock lock(state.mutex);
        if (state.detached) continue;
        std::vector<u8>& pending = which == Source::Stdout ? state.pending_stdout : state.pending_stderr;
        pending.insert(pending.end(), core.read_buffer.begin(), core.read_buffer.begin() + got);
        flush_output(state, lock, which);
        if (pending.size() >= kMaxPending && !to_end) {
            bool& paused = which == Source::Stdout ? state.stdout_paused : state.stderr_paused;
            core.unwatch(fd.get());
            paused = true;
            return;
        }
    }
}

[[nodiscard]] ports::ChildExit to_child_exit(const WaitResult& waited) noexcept {
    ports::ChildExit exit;
    if (waited.outcome != WaitOutcome::Ended) return exit;
    if (waited.status.code == CLD_EXITED)
        exit.code = waited.status.status;
    else
        exit.signal = waited.status.status;
    return exit;
}

void erase_if_done(LoopCore& core, ChildState& state) {
    bool done = false;
    {
        const std::lock_guard lock(state.mutex);
        done = state.detached && state.reaped;
    }
    if (done) core.children.erase(state.id);
}

void on_exit_ready(LoopCore& core, const std::shared_ptr<ChildState>& shared) {
    ChildState& state = *shared;
    ports::ChildExit exit;
    {
        const std::lock_guard lock(state.mutex);
        if (state.exited || state.reaped) return;
        PeekResult peeked;
        if (state.detached)
            peeked.waited = reap_listed_child(state.pid);
        else
            peeked = peek_listed_child(state.pid);
        if (peeked.waited.outcome == WaitOutcome::Running) return;
        core.unwatch(state.pidfd.get());
        if (peeked.zombie) {
            state.exited = true;
        } else {
            state.reaped = true;
            state.pidfd.reset();
        }
        exit = to_child_exit(peeked.waited);
    }
    // What the child wrote before exiting is in the pipes now; later output of descendants that
    // kept them open is not its own.
    for (const Source which : {Source::Stdout, Source::Stderr}) {
        read_output(core, state, which, true);
        close_output(core, state, which);
    }
    std::unique_lock lock(state.mutex);
    state.pending_exit = exit;
    flush_exit(state, lock);
    lock.unlock();
    erase_if_done(core, state);
}

void on_stdin_ready(LoopCore& core, ChildState& state) {
    const std::lock_guard lock(state.mutex);
    while (!state.stdin_queue.empty() && state.stdin_fd.valid()) {
        const ssize_t sent = ::send(state.stdin_fd.get(), state.stdin_queue.data(), state.stdin_queue.size(),
                                    MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent < 0 && errno == EINTR) continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (sent < 0) {
            close_stdin_now(core, state);
            return;
        }
        state.stdin_queue.erase(state.stdin_queue.begin(), state.stdin_queue.begin() + sent);
    }
    if (state.stdin_watched) core.unwatch(state.stdin_fd.get());
    state.stdin_watched = false;
    state.stdin_watch_requested = false;
    if (state.stdin_close_requested) close_stdin_now(core, state);
}

void dispatch(LoopCore& core, u64 data) {
    const auto source = static_cast<Source>(data & 0xFF);
    const auto found = core.children.find(data >> 8);
    if (found == core.children.end()) return;
    const std::shared_ptr<ChildState> state = found->second;
    switch (source) {
        case Source::Exit: on_exit_ready(core, state); break;
        case Source::Stdout:
        case Source::Stderr: read_output(core, *state, source, false); break;
        case Source::Stdin: on_stdin_ready(core, *state); break;
        case Source::Wake:
        case Source::ChildSignal: break;
    }
}

void run_loop(LoopCore& core) {
    std::array<epoll_event, kMaxEvents> events{};
    for (;;) {
        const int count = ::epoll_wait(core.epoll.get(), events.data(), kMaxEvents, -1);
        if (count < 0) {
            if (errno == EINTR) continue;
            REBOOT_LOG_ERROR(Engine, "epoll_wait failed with errno {}; child processes are no longer watched", errno);
            return;
        }
        for (int i = 0; i < count; ++i) {
            const u64 data = events[static_cast<std::size_t>(i)].data.u64;
            const auto source = static_cast<Source>(data & 0xFF);
            if (source == Source::Wake) {
                u64 drained = 0;
                (void)::read(core.wake.get(), &drained, sizeof drained);
                std::vector<Command> commands;
                {
                    const std::lock_guard lock(core.mutex);
                    if (core.stopping) return;
                    commands.swap(core.commands);
                }
                for (Command& command : commands) command(core);
            } else if (source == Source::ChildSignal) {
                std::array<char, 64> drained{};
                while (::read(core.child_signal_read.get(), drained.data(), drained.size()) > 0) {
                }
                reap_zombies();
            } else {
                dispatch(core, data);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The ChildProcess handed to callers.

class PidfdChild final : public ports::ChildProcess {
public:
    PidfdChild(std::shared_ptr<LoopCore> core, std::shared_ptr<ChildState> state, NativePath runtime_dir,
               UniqueFunction<void(UniqueFunction<void()>)> run_later)
        : core_(std::move(core)), state_(std::move(state)), runtime_dir_(std::move(runtime_dir)),
          run_later_(std::move(run_later)) {}

    ~PidfdChild() override {
        {
            std::unique_lock lock(state_->mutex);
            state_->detached = true;
            if (!on_loop_thread(*core_)) state_->idle.wait(lock, [&] { return state_->delivering == 0; });
            state_->on_stdout = nullptr;
            state_->on_stderr = nullptr;
            state_->on_exit = nullptr;
            state_->pending_stdout.clear();
            state_->pending_stderr.clear();
            // Our own children stop on stdin EOF.
            state_->stdin_close_requested = true;
        }
        core_->post([state = state_](LoopCore& core) {
            {
                const std::lock_guard lock(state->mutex);
                if (state->stdin_queue.empty()) close_stdin_now(core, *state);
                if (state->exited && !state->reaped) {
                    (void)reap_listed_child(state->pid);
                    state->reaped = true;
                    state->pidfd.reset();
                }
            }
            close_output(core, *state, Source::Stdout);
            close_output(core, *state, Source::Stderr);
            erase_if_done(core, *state);
        });
    }

    PidfdChild(const PidfdChild&) = delete;
    PidfdChild& operator=(const PidfdChild&) = delete;

    [[nodiscard]] u32 pid() const override { return state_->pid; }
    [[nodiscard]] SystemTime created() const override { return state_->created; }

    void write_stdin(std::span<const u8> bytes) override {
        const std::lock_guard lock(state_->mutex);
        if (!state_->stdin_fd.valid() || state_->stdin_close_requested || bytes.empty()) return;
        if (state_->stdin_queue.empty()) {
            while (!bytes.empty()) {
                const ssize_t sent =
                    ::send(state_->stdin_fd.get(), bytes.data(), bytes.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
                if (sent < 0 && errno == EINTR) continue;
                if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                // The child closed its end; what it would have read is moot.
                if (sent < 0) return;
                bytes = bytes.subspan(static_cast<std::size_t>(sent));
            }
            if (bytes.empty()) return;
        }
        state_->stdin_queue.insert(state_->stdin_queue.end(), bytes.begin(), bytes.end());
        request_stdin_watch();
    }

    void close_stdin() override {
        {
            const std::lock_guard lock(state_->mutex);
            if (state_->stdin_close_requested) return;
            state_->stdin_close_requested = true;
        }
        core_->post([state = state_](LoopCore& core) {
            const std::lock_guard lock(state->mutex);
            if (state->stdin_queue.empty()) close_stdin_now(core, *state);
        });
    }

    void on_stdout(OutputCallback callback) override { set_output(&ChildState::on_stdout, std::move(callback), Source::Stdout); }
    void on_stderr(OutputCallback callback) override { set_output(&ChildState::on_stderr, std::move(callback), Source::Stderr); }

    void on_exit(UniqueFunction<void(ports::ChildExit)> callback) override {
        {
            const std::lock_guard lock(state_->mutex);
            if (state_->detached) return;
            state_->on_exit = std::move(callback);
            if (!state_->pending_exit) return;
        }
        core_->post([state = state_](LoopCore&) {
            std::unique_lock lock(state->mutex);
            flush_exit(*state, lock);
        });
    }

    Result<void> terminate_tree() override {
        std::optional<Diagnostic> failure;
        std::optional<std::string> unit;
        {
            const std::lock_guard lock(state_->mutex);
            // Until reaped, even as a zombie, the group id and the pidfd still name this child, so
            // neither can hit a reused pid; the group may outlive its leader.
            if (!state_->reaped) {
                if (state_->own_group && ::kill(-static_cast<pid_t>(state_->pid), SIGKILL) != 0 && errno != ESRCH)
                    failure = posix::call_failed("kill", errno);
                if (!state_->exited && state_->pidfd.valid() && sys_pidfd_send_signal(state_->pidfd.get(), SIGKILL) != 0 &&
                    errno != ESRCH && !failure)
                    failure = posix::call_failed("pidfd_send_signal", errno);
            }
            unit = state_->scope_unit;
        }
        if (unit) {
            run_later_([scope = std::move(*unit), runtime_dir = runtime_dir_] {
                const Result<HelperResult> stopped = systemctl_user({"stop", scope}, runtime_dir);
                // Exit 5 is "not loaded": the scope already went with its last process.
                if (!stopped || (stopped->exit_code != 0 && stopped->exit_code != 5))
                    REBOOT_LOG_WARN(Engine, "The systemd scope {} could not be stopped", scope);
            });
        }
        if (failure) return std::unexpected(std::move(*failure));
        return {};
    }

private:
    void set_output(OutputCallback ChildState::*slot, OutputCallback callback, Source which) {
        {
            const std::lock_guard lock(state_->mutex);
            if (state_->detached) return;
            (*state_).*slot = std::move(callback);
        }
        core_->post([state = state_, which](LoopCore& core) {
            resume_output(core, *state, which);
            std::unique_lock lock(state->mutex);
            flush_output(*state, lock, which);
            flush_exit(*state, lock);
        });
    }

    // Requires the state's mutex.
    void request_stdin_watch() {
        if (state_->stdin_watch_requested) return;
        state_->stdin_watch_requested = true;
        core_->post([state = state_](LoopCore& core) {
            const std::lock_guard lock(state->mutex);
            if (state->stdin_watched || !state->stdin_fd.valid()) return;
            state->stdin_watched = true;
            core.watch(state->stdin_fd.get(), state->id, Source::Stdin, EPOLLOUT);
        });
    }

    std::shared_ptr<LoopCore> core_;
    std::shared_ptr<ChildState> state_;
    NativePath runtime_dir_;
    UniqueFunction<void(UniqueFunction<void()>)> run_later_;
};

// ---------------------------------------------------------------------------------------------
// Spawning.

// Everything the child needs, prepared before the clone so the child calls only
// async-signal-safe functions.
struct ChildPlan {
    const char* path = nullptr;
    char* const* argv = nullptr;
    char* const* envp = nullptr;
    const char* cwd = nullptr;
    int stdin_fd = -1;
    int stdout_fd = -1;
    int stderr_fd = -1;
    int error_fd = -1;
    bool own_group = true;
    pid_t parent = 0;
    int fd_limit = 0;
};

void close_from(int first, int last) noexcept {
    for (int fd = first; fd < last; ++fd) ::close(fd);
}

[[noreturn]] void report_and_exit(int error_fd, int error) noexcept {
    (void)::write(error_fd, &error, sizeof error);
    ::_exit(kExecFailedStatus);
}

[[noreturn]] void run_child(const ChildPlan& plan) noexcept {
    if (::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) report_and_exit(plan.error_fd, errno);
    // The engine died before PR_SET_PDEATHSIG took effect.
    if (::getppid() != plan.parent) ::_exit(kExecFailedStatus);
    if (plan.own_group && ::setpgid(0, 0) != 0) report_and_exit(plan.error_fd, errno);

    struct sigaction defaults {};
    defaults.sa_handler = SIG_DFL;
    sigemptyset(&defaults.sa_mask);
    for (int number = 1; number < NSIG; ++number) {
        if (number != SIGKILL && number != SIGSTOP) (void)::sigaction(number, &defaults, nullptr);
    }
    sigset_t empty;
    sigemptyset(&empty);
    (void)::sigprocmask(SIG_SETMASK, &empty, nullptr);

    if (::dup2(plan.stdin_fd, STDIN_FILENO) < 0 || ::dup2(plan.stdout_fd, STDOUT_FILENO) < 0 ||
        ::dup2(plan.stderr_fd, STDERR_FILENO) < 0)
        report_and_exit(plan.error_fd, errno);
    if (plan.cwd != nullptr && ::chdir(plan.cwd) != 0) report_and_exit(plan.error_fd, errno);

    const auto upper = static_cast<unsigned int>(-1);
    bool ranged = true;
    if (plan.error_fd > 3) ranged = ::syscall(kSysCloseRange, 3U, static_cast<unsigned int>(plan.error_fd - 1), 0U) == 0;
    if (ranged) ranged = ::syscall(kSysCloseRange, static_cast<unsigned int>(plan.error_fd + 1), upper, 0U) == 0;
    if (!ranged) {
        close_from(3, plan.error_fd);
        close_from(plan.error_fd + 1, plan.fd_limit);
    }
    ::execve(plan.path, plan.argv, plan.envp);
    report_and_exit(plan.error_fd, errno);
}

struct Pipe {
    posix::UniqueFd read;
    posix::UniqueFd write;
};

[[nodiscard]] Result<Pipe> make_pipe() {
    std::array<int, 2> ends{-1, -1};
    if (::pipe2(ends.data(), O_CLOEXEC) != 0) return std::unexpected(posix::call_failed("pipe2", errno));
    return Pipe{posix::UniqueFd{ends[0]}, posix::UniqueFd{ends[1]}};
}

// The child's dup2 onto 0-2 would leave a source numbered 0-2 close-on-exec or clobber it.
[[nodiscard]] Result<void> above_stdio(posix::UniqueFd& fd) {
    if (fd.get() > STDERR_FILENO) return {};
    const int moved = ::fcntl(fd.get(), F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    if (moved < 0) return std::unexpected(posix::call_failed("fcntl", errno));
    fd.reset(moved);
    return {};
}

[[nodiscard]] std::vector<char*> pointers(std::vector<std::string>& strings) {
    std::vector<char*> out;
    out.reserve(strings.size() + 1);
    for (std::string& text : strings) out.push_back(text.data());
    out.push_back(nullptr);
    return out;
}

[[nodiscard]] bool has_var(const ports::EnvBlock& env, std::string_view name) {
    return std::ranges::any_of(env.vars, [&](const auto& var) { return var.first == name; });
}

void kill_and_reap(pid_t pid) noexcept {
    ::kill(pid, SIGKILL);
    for (;;) {
        siginfo_t info{};
        if (::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED) == 0 || errno != EINTR) break;
    }
}

struct Spawned {
    pid_t pid = 0;
    posix::UniqueFd pidfd;
};

// clone3 hands back a pidfd race-free; where seccomp refuses clone3, clone and pidfd_open, which
// is safe too since the child cannot be reaped (and its pid reused) before we wait for it.
[[nodiscard]] Result<Spawned> clone_child(const ChildPlan& plan) {
    int pidfd = -1;
    CloneArgs args;
    args.flags = kClonePidfd | CLONE_VFORK;
    args.pidfd = reinterpret_cast<u64>(&pidfd);
    args.exit_signal = SIGCHLD;
    long pid = ::syscall(kSysClone3, &args, sizeof args);
    if (pid == 0) run_child(plan);
    if (pid > 0) return Spawned{static_cast<pid_t>(pid), posix::UniqueFd{pidfd}};
    if (errno != ENOSYS && errno != EPERM && errno != E2BIG) return std::unexpected(posix::call_failed("clone3", errno));

    // Full-width arguments: the kernel reads every register as a long.
    pid = ::syscall(SYS_clone, static_cast<unsigned long>(CLONE_VFORK | SIGCHLD), nullptr, nullptr, nullptr, 0UL);
    if (pid == 0) run_child(plan);
    if (pid < 0) return std::unexpected(posix::call_failed("clone", errno));
    posix::UniqueFd opened{sys_pidfd_open(static_cast<pid_t>(pid))};
    if (!opened.valid()) {
        const int error = errno;
        kill_and_reap(static_cast<pid_t>(pid));
        return std::unexpected(posix::call_failed("pidfd_open", error));
    }
    return Spawned{static_cast<pid_t>(pid), std::move(opened)};
}

// One thread for spawning and for the scope stops; children's PDEATHSIG names this thread.
class SpawnWorker {
public:
    SpawnWorker() : thread_([this] { run(); }) {}
    ~SpawnWorker() {
        {
            const std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        changed_.notify_all();
        thread_.join();
    }
    SpawnWorker(const SpawnWorker&) = delete;
    SpawnWorker& operator=(const SpawnWorker&) = delete;

    void submit(UniqueFunction<void()> job) {
        {
            const std::lock_guard lock(mutex_);
            jobs_.push_back(std::move(job));
        }
        changed_.notify_all();
    }

private:
    void run() {
        // A signal handler must never run in a child between its clone and its exec.
        sigset_t all;
        sigfillset(&all);
        ::pthread_sigmask(SIG_BLOCK, &all, nullptr);
        for (;;) {
            UniqueFunction<void()> job;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
                if (jobs_.empty()) return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            try {
                job();
            } catch (...) {
                REBOOT_LOG_ERROR(Engine, "internal.bug: a process launcher job failed");
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<UniqueFunction<void()>> jobs_;
    bool stopping_ = false;
    std::thread thread_;
};

}  // namespace

Result<std::optional<std::chrono::system_clock::time_point>> read_proc_start_time(u32 pid) {
    const NativePath path = NativePath{"/proc"} / std::to_string(pid) / "stat";
    Result<std::string> text = read_text_file(path);
    if (!text) {
        if (is_missing(text.error())) return std::nullopt;
        return std::unexpected(std::move(text.error()));
    }
    const std::optional<ProcStat> stat = parse_proc_stat(*text);
    if (!stat) return std::unexpected(posix::call_failed("read", EINVAL, path));
    const NativePath boot_path{"/proc/stat"};
    Result<std::string> boot_text = read_text_file(boot_path);
    if (!boot_text) return std::unexpected(std::move(boot_text.error()));
    const std::optional<u64> boot = parse_boot_time(*boot_text);
    const long hz = ::sysconf(_SC_CLK_TCK);
    if (!boot || hz <= 0) return std::unexpected(posix::call_failed("read", EINVAL, boot_path));
    const auto ticks = static_cast<u64>(hz);
    const auto since_boot = std::chrono::seconds{stat->start_ticks / ticks} +
                            std::chrono::nanoseconds{(stat->start_ticks % ticks) * 1'000'000'000ULL / ticks};
    return std::chrono::system_clock::time_point{std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::seconds{*boot} + since_boot)};
}

struct PidfdProcessLauncher::Impl {
    std::optional<SystemdScopes> scopes;
    std::shared_ptr<LoopCore> core = std::make_shared<LoopCore>();
    std::optional<Diagnostic> init_error;
    std::thread loop;
    // Children hold it weakly, for scope stops after the launcher is gone.
    std::shared_ptr<SpawnWorker> worker;
    u64 next_id = 1;
    u64 next_scope = 1;

    Result<std::unique_ptr<ports::ChildProcess>> spawn_now(const ports::ProcessLaunch& launch);
};

Result<std::unique_ptr<ports::ChildProcess>> PidfdProcessLauncher::Impl::spawn_now(const ports::ProcessLaunch& launch) {
    std::optional<std::string> scope_unit;
    std::vector<std::string> argv_storage;
    ports::EnvBlock env = launch.env;
    if (launch.scope_name && scopes) {
        scope_unit = *launch.scope_name + "-" + std::to_string(::getpid()) + "-" + std::to_string(next_scope++) + ".scope";
        argv_storage = {scopes->systemd_run.native(), "--user", "--scope", "--quiet", "--collect",
                        "--unit=" + *scope_unit, "--"};
        if (!has_var(env, "XDG_RUNTIME_DIR")) {
            argv_storage.insert(argv_storage.end(), {"/usr/bin/env", "-u", "XDG_RUNTIME_DIR"});
            env.vars.emplace_back("XDG_RUNTIME_DIR", scopes->runtime_dir.native());
        }
        argv_storage.push_back(launch.exe.native());
    } else {
        argv_storage.push_back(launch.exe.native());
    }
    argv_storage.insert(argv_storage.end(), launch.args.begin(), launch.args.end());
    const std::string path = argv_storage.front();
    std::vector<char*> argv = pointers(argv_storage);
    std::vector<std::string> env_storage = envp_strings(env.vars);
    std::vector<char*> envp = pointers(env_storage);

    std::array<int, 2> stdin_ends{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, stdin_ends.data()) != 0)
        return std::unexpected(posix::call_failed("socketpair", errno));
    posix::UniqueFd stdin_parent{stdin_ends[0]};
    posix::UniqueFd stdin_child{stdin_ends[1]};
    const int flags = ::fcntl(stdin_parent.get(), F_GETFL);
    if (flags < 0 || ::fcntl(stdin_parent.get(), F_SETFL, flags | O_NONBLOCK) != 0)
        return std::unexpected(posix::call_failed("fcntl", errno));
    ::shutdown(stdin_parent.get(), SHUT_RD);

    std::optional<Pipe> out;
    std::optional<Pipe> err;
    posix::UniqueFd null_out;
    if (launch.stdio != ports::StdioMode::Null) {
        auto made_out = make_pipe();
        if (!made_out) return std::unexpected(std::move(made_out.error()));
        auto made_err = make_pipe();
        if (!made_err) return std::unexpected(std::move(made_err.error()));
        out.emplace(std::move(*made_out));
        err.emplace(std::move(*made_err));
        for (Pipe* ends : {&*out, &*err}) {
            const int status = ::fcntl(ends->read.get(), F_GETFL);
            if (status < 0 || ::fcntl(ends->read.get(), F_SETFL, status | O_NONBLOCK) != 0)
                return std::unexpected(posix::call_failed("fcntl", errno));
            if (auto moved = above_stdio(ends->write); !moved) return std::unexpected(std::move(moved.error()));
        }
    } else {
        null_out.reset(::open("/dev/null", O_WRONLY | O_CLOEXEC));
        if (!null_out.valid()) return std::unexpected(posix::call_failed("open", errno, NativePath{"/dev/null"}));
        if (auto moved = above_stdio(null_out); !moved) return std::unexpected(std::move(moved.error()));
    }
    if (auto moved = above_stdio(stdin_child); !moved) return std::unexpected(std::move(moved.error()));
    auto exec_error = make_pipe();
    if (!exec_error) return std::unexpected(std::move(exec_error.error()));
    if (auto moved = above_stdio(exec_error->write); !moved) return std::unexpected(std::move(moved.error()));

    rlimit limit{};
    const int fd_limit =
        ::getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY
            ? static_cast<int>(std::min<rlim_t>(limit.rlim_cur, 1 << 20))
            : 1 << 16;

    const std::string cwd = launch.cwd.native();
    ChildPlan plan;
    plan.path = path.c_str();
    plan.argv = argv.data();
    plan.envp = envp.data();
    plan.cwd = cwd.empty() ? nullptr : cwd.c_str();
    plan.stdin_fd = stdin_child.get();
    plan.stdout_fd = out ? out->write.get() : null_out.get();
    plan.stderr_fd = err ? err->write.get() : null_out.get();
    plan.error_fd = exec_error->write.get();
    plan.own_group = launch.own_group;
    plan.parent = ::getpid();
    plan.fd_limit = fd_limit;

    Spawned spawned;
    {
        const auto held = ChildTable::lock();
        Result<Spawned> cloned = clone_child(plan);
        if (!cloned) return std::unexpected(std::move(cloned.error()));
        spawned = std::move(*cloned);
        ChildTable::add(static_cast<u32>(spawned.pid));
    }
    const auto pid = static_cast<u32>(spawned.pid);
    stdin_child.reset();
    null_out.reset();
    if (out) out->write.reset();
    if (err) err->write.reset();
    exec_error->write.reset();

    const auto abandon = [&](Diagnostic failure) -> Result<std::unique_ptr<ports::ChildProcess>> {
        ::kill(spawned.pid, SIGKILL);
        while (reap_listed_child(pid).outcome == WaitOutcome::Running) {
            pollfd entry{.fd = spawned.pidfd.get(), .events = POLLIN, .revents = 0};
            (void)::poll(&entry, 1, -1);
        }
        return std::unexpected(std::move(failure));
    };

    int child_errno = 0;
    for (;;) {
        const ssize_t got = ::read(exec_error->read.get(), &child_errno, sizeof child_errno);
        if (got < 0 && errno == EINTR) continue;
        if (got == static_cast<ssize_t>(sizeof child_errno))
            return abandon(posix::call_failed("execve", child_errno, NativePath{path}));
        break;
    }

    auto created = read_proc_start_time(pid);
    if (!created) return abandon(std::move(created.error()));
    if (!*created) return abandon(posix::call_failed("execve", ESRCH, NativePath{path}));

    auto state = std::make_shared<ChildState>();
    state->id = next_id++;
    state->pid = pid;
    state->created = **created;
    state->own_group = launch.own_group;
    state->scope_unit = std::move(scope_unit);
    state->pidfd = std::move(spawned.pidfd);
    state->stdin_fd = std::move(stdin_parent);
    if (out) state->stdout_fd = std::move(out->read);
    if (err) state->stderr_fd = std::move(err->read);

    core->post([state](LoopCore& loop_core) {
        loop_core.children.emplace(state->id, state);
        loop_core.watch(state->pidfd.get(), state->id, Source::Exit, EPOLLIN);
        if (state->stdout_fd.valid()) loop_core.watch(state->stdout_fd.get(), state->id, Source::Stdout, EPOLLIN);
        if (state->stderr_fd.valid()) loop_core.watch(state->stderr_fd.get(), state->id, Source::Stderr, EPOLLIN);
    });

    return std::make_unique<PidfdChild>(core, std::move(state), scopes ? scopes->runtime_dir : NativePath{},
                                        [jobs = std::weak_ptr<SpawnWorker>(worker)](UniqueFunction<void()> job) {
                                            if (const std::shared_ptr<SpawnWorker> alive = jobs.lock())
                                                alive->submit(std::move(job));
                                        });
}

PidfdProcessLauncher::PidfdProcessLauncher(std::optional<SystemdScopes> scopes) : impl_(std::make_unique<Impl>()) {
    impl_->scopes = std::move(scopes);
    LoopCore& core = *impl_->core;
    core.epoll.reset(::epoll_create1(EPOLL_CLOEXEC));
    core.wake.reset(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
    if (!core.epoll.valid() || !core.wake.valid()) {
        impl_->init_error = posix::call_failed(core.epoll.valid() ? "eventfd" : "epoll_create1", errno);
        return;
    }
    core.watch(core.wake.get(), 0, Source::Wake, EPOLLIN);

    if (::getpid() == 1) {
        std::array<int, 2> ends{-1, -1};
        if (::pipe2(ends.data(), O_CLOEXEC | O_NONBLOCK) == 0) {
            core.child_signal_read.reset(ends[0]);
            core.child_signal_write.reset(ends[1]);
            g_child_signal_fd.store(ends[1], std::memory_order_relaxed);
            struct sigaction action {};
            action.sa_handler = on_child_signal;
            sigemptyset(&action.sa_mask);
            action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
            if (::sigaction(SIGCHLD, &action, nullptr) == 0)
                core.watch(core.child_signal_read.get(), 0, Source::ChildSignal, EPOLLIN);
        }
    }

    impl_->loop = std::thread([shared = impl_->core] {
        try {
            run_loop(*shared);
        } catch (...) {
            REBOOT_LOG_ERROR(Engine, "internal.bug: the child process thread failed");
        }
    });
    core.thread = impl_->loop.get_id();
    impl_->worker = std::make_shared<SpawnWorker>();
}

PidfdProcessLauncher::~PidfdProcessLauncher() {
    impl_->worker.reset();
    if (!impl_->loop.joinable()) return;
    {
        const std::lock_guard lock(impl_->core->mutex);
        impl_->core->stopping = true;
    }
    impl_->core->post([](LoopCore&) {});
    impl_->loop.join();
}

Result<std::unique_ptr<ports::ChildProcess>> PidfdProcessLauncher::spawn(const ports::ProcessLaunch& launch) {
    if (impl_->init_error) return std::unexpected(*impl_->init_error);
    std::mutex mutex;
    std::condition_variable done;
    std::optional<Result<std::unique_ptr<ports::ChildProcess>>> result;
    impl_->worker->submit([&] {
        Result<std::unique_ptr<ports::ChildProcess>> spawned = impl_->spawn_now(launch);
        const std::lock_guard lock(mutex);
        result.emplace(std::move(spawned));
        done.notify_all();
    });
    std::unique_lock lock(mutex);
    done.wait(lock, [&] { return result.has_value(); });
    return std::move(*result);
}

Result<bool> PidfdProcessLauncher::is_alive(u32 pid, std::chrono::system_clock::time_point created) {
    auto started = read_proc_start_time(pid);
    if (!started) return std::unexpected(std::move(started.error()));
    if (!started->has_value() || !posix::same_start_time(created, **started)) return false;
    // A zombie has ended; ours stay one until their handle goes.
    const std::optional<std::string> text = try_read_text_file(NativePath{"/proc"} / std::to_string(pid) / "stat");
    const std::optional<ProcStat> stat = text ? parse_proc_stat(*text) : std::nullopt;
    return stat && stat->state != 'Z' && stat->state != 'X';
}

Result<void> PidfdProcessLauncher::kill(u32 pid, std::chrono::system_clock::time_point created) {
    const auto target = static_cast<pid_t>(pid);
    // Opened before the check, so the signal reaches the process that was checked.
    const posix::UniqueFd pidfd{sys_pidfd_open(target)};
    if (!pidfd.valid()) {
        if (errno == ESRCH) return {};
        return std::unexpected(posix::call_failed("pidfd_open", errno));
    }
    auto alive = is_alive(pid, created);
    if (!alive) return std::unexpected(std::move(alive.error()));
    if (!*alive) return {};
    if (::getpgid(target) == target && ::kill(-target, SIGKILL) != 0 && errno != ESRCH)
        return std::unexpected(posix::call_failed("kill", errno));
    if (sys_pidfd_send_signal(pidfd.get(), SIGKILL) != 0 && errno != ESRCH)
        return std::unexpected(posix::call_failed("pidfd_send_signal", errno));
    return {};
}

}  // namespace rb::os_linux::platform
