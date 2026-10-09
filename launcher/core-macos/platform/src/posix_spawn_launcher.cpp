#include "darwin.hpp"

#include "reboot/os_macos/platform/posix_spawn_launcher.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/event.h>
#include <sys/wait.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "kevent.hpp"
#include "proc_info.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/posix_spawner.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "wait_status.hpp"
#include "watchdog_script.hpp"

namespace reboot::os_macos::platform {

namespace {

using namespace std::chrono_literals;
using Bytes = std::vector<u8>;
using OutputCallback = UniqueFunction<void(std::span<const u8>)>;

// A grandchild that inherited an output pipe would otherwise hold back the exit.
constexpr std::chrono::milliseconds kOutputDrainBound = 2s;
constexpr std::size_t kReadChunk = 64 * 1024;

// kevent udata carries the child id and what the event is about.
enum class Tag : std::uintptr_t { Stdout = 0, Stderr = 1, Stdin = 2, Child = 3, Watchdog = 4, Drain = 5 };
constexpr std::uintptr_t kTagBits = 3;
constexpr std::uintptr_t kWakeIdent = 0;

[[nodiscard]] void* encode(u64 id, Tag tag) noexcept {
    return reinterpret_cast<void*>((static_cast<std::uintptr_t>(id) << kTagBits) | static_cast<std::uintptr_t>(tag));
}
[[nodiscard]] u64 id_of(const void* udata) noexcept { return reinterpret_cast<std::uintptr_t>(udata) >> kTagBits; }
[[nodiscard]] Tag tag_of(const void* udata) noexcept {
    return static_cast<Tag>(reinterpret_cast<std::uintptr_t>(udata) & ((std::uintptr_t{1} << kTagBits) - 1));
}

[[nodiscard]] bool set_nonblocking(int fd) noexcept {
    const int flags = ::fcntl(fd, F_GETFL);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

[[nodiscard]] int reap(pid_t pid) noexcept {
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return 0;
    }
    return status;
}

// What a child shares between its ChildProcess, the callers' threads and the kqueue thread.
struct ChildState {
    u64 id = 0;
    pid_t pid = 0;
    pid_t watchdog_pid = 0;
    // The child leads its own process group, which its watchdog joins.
    bool group = true;
    bool control_channel = false;

    // Serialises callbacks and the exit, so output always lands before it. Recursive, since a
    // callback may call back into its ChildProcess.
    std::recursive_mutex output_mutex;
    std::array<OutputCallback, 2> on_output;
    std::array<Bytes, 2> held_output;
    UniqueFunction<void(ports::ChildExit)> on_exit;
    std::optional<ports::ChildExit> held_exit;
    bool closed = false;

    std::mutex stdin_mutex;
    posix::UniqueFd stdin_fd;
    std::deque<Bytes> outbox;
    bool stdin_closing = false;

    // Reaping and terminate_tree's checks happen under this, so no signal reaches a reused pid.
    std::mutex reap_mutex;
    bool child_reaped = false;
    bool watchdog_reaped = false;

    // kqueue thread only.
    std::array<posix::UniqueFd, 2> outputs;
    bool child_exited = false;
    std::optional<ports::ChildExit> exit;
    bool exit_delivered = false;
    bool drain_armed = false;
    bool released = false;

    void deliver_output(std::size_t stream, std::span<const u8> bytes, bool on_loop) {
        std::scoped_lock lock(output_mutex);
        if (closed) {
            drop_callbacks(on_loop);
            return;
        }
        if (on_output[stream]) {
            on_output[stream](bytes);
        } else {
            held_output[stream].insert(held_output[stream].end(), bytes.begin(), bytes.end());
        }
        if (closed) drop_callbacks(on_loop);
    }

    void deliver_exit(ports::ChildExit child_exit, bool on_loop) {
        std::scoped_lock lock(output_mutex);
        if (closed) {
            drop_callbacks(on_loop);
            return;
        }
        if (on_exit) {
            UniqueFunction<void(ports::ChildExit)> callback = std::move(on_exit);
            on_exit = nullptr;
            callback(child_exit);
        } else {
            held_exit = child_exit;
        }
        if (closed) drop_callbacks(on_loop);
    }

    // On the kqueue thread a callback of this child may still be on the stack; callbacks are then
    // dropped by the next delivery, once it has returned.
    void drop_callbacks(bool on_loop) {
        if (on_loop) return;
        on_output = {};
        on_exit = nullptr;
    }

    // Writes what the pipe takes now; false when the outbox still holds bytes. Under stdin_mutex.
    [[nodiscard]] bool flush_stdin() {
        while (!outbox.empty() && stdin_fd.valid()) {
            Bytes& front = outbox.front();
            const ssize_t written = ::write(stdin_fd.get(), front.data(), front.size());
            if (written < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN) return false;
                // The child closed its stdin or died: nothing more can reach it.
                outbox.clear();
                stdin_fd.reset();
                return true;
            }
            front.erase(front.begin(), front.begin() + written);
            if (front.empty()) outbox.pop_front();
        }
        if (stdin_closing) stdin_fd.reset();
        return true;
    }
};

// The single kqueue thread: output, stdin backlog, exits, reaping and the drain bound.
class Loop {
public:
    Loop() : queue_(::kqueue()) {}
    ~Loop() { stop(); }
    Loop(const Loop&) = delete;
    Loop& operator=(const Loop&) = delete;

    [[nodiscard]] Result<void> start() {
        if (!queue_.valid()) return std::unexpected(posix::call_failed("kqueue", errno));
        if (::fcntl(queue_.get(), F_SETFD, FD_CLOEXEC) != 0) return std::unexpected(posix::call_failed("fcntl", errno));
        const struct kevent wake = make_kevent(kWakeIdent, EVFILT_USER, EV_ADD | EV_CLEAR);
        if (::kevent(queue_.get(), &wake, 1, nullptr, 0, nullptr) != 0)
            return std::unexpected(posix::call_failed("kevent", errno));
        thread_ = std::thread([this] { run(); });
        return {};
    }

    void stop() {
        {
            std::scoped_lock lock(commands_mutex_);
            if (stopping_) return;
            stopping_ = true;
        }
        wake();
        if (thread_.joinable()) thread_.join();
    }

    [[nodiscard]] bool on_loop_thread() const noexcept { return std::this_thread::get_id() == thread_id_.load(); }

    void add(std::shared_ptr<ChildState> state) { post(Command{CommandKind::Add, std::move(state)}); }
    void release(std::shared_ptr<ChildState> state) { post(Command{CommandKind::Release, std::move(state)}); }

    // Arms a one-shot wait for `state`'s stdin to drain. Under the state's stdin_mutex.
    void watch_stdin(const ChildState& state) {
        const struct kevent change = make_kevent(static_cast<std::uintptr_t>(state.stdin_fd.get()), EVFILT_WRITE,
                                                 EV_ADD | EV_ONESHOT, 0, 0, encode(state.id, Tag::Stdin));
        (void)::kevent(queue_.get(), &change, 1, nullptr, 0, nullptr);
    }

private:
    enum class CommandKind : u8 { Add, Release };
    struct Command {
        CommandKind kind;
        std::shared_ptr<ChildState> state;
    };

    void post(Command command) {
        {
            std::scoped_lock lock(commands_mutex_);
            if (stopping_) return;
            commands_.push_back(std::move(command));
        }
        wake();
    }

    void wake() {
        const struct kevent trigger = make_kevent(kWakeIdent, EVFILT_USER, 0, NOTE_TRIGGER);
        (void)::kevent(queue_.get(), &trigger, 1, nullptr, 0, nullptr);
    }

    void run() {
        thread_id_.store(std::this_thread::get_id());
        std::array<struct kevent, 32> events{};
        for (;;) {
            const int count = ::kevent(queue_.get(), nullptr, 0, events.data(), static_cast<int>(events.size()), nullptr);
            if (count < 0) {
                if (errno == EINTR) continue;
                REBOOT_LOG_ERROR(Engine, "internal.bug: the process launcher's kevent failed with errno {}", errno);
                return;
            }
            try {
                for (int i = 0; i < count; ++i) handle(events[static_cast<std::size_t>(i)]);
                if (!drain_commands()) return;
            } catch (...) {
                REBOOT_LOG_ERROR(Engine, "internal.bug: the process launcher's kqueue thread threw");
            }
        }
    }

    // False once the loop is stopping.
    [[nodiscard]] bool drain_commands() {
        std::deque<Command> commands;
        bool stopping = false;
        {
            std::scoped_lock lock(commands_mutex_);
            commands.swap(commands_);
            stopping = stopping_;
        }
        for (Command& command : commands) {
            if (command.kind == CommandKind::Add) {
                add_child(command.state);
            } else {
                release_child(*command.state);
            }
        }
        return !stopping;
    }

    void add_child(const std::shared_ptr<ChildState>& state) {
        ChildState& child = *state;
        children_[child.id] = state;
        for (std::size_t stream = 0; stream < child.outputs.size(); ++stream) {
            if (!child.outputs[stream].valid()) continue;
            const struct kevent read = make_kevent(static_cast<std::uintptr_t>(child.outputs[stream].get()), EVFILT_READ,
                                                   EV_ADD, 0, 0, encode(child.id, static_cast<Tag>(stream)));
            if (::kevent(queue_.get(), &read, 1, nullptr, 0, nullptr) != 0) child.outputs[stream].reset();
        }
        watch_exit(child, child.pid, Tag::Child);
        watch_exit(child, child.watchdog_pid, Tag::Watchdog);
    }

    // A process that exited before the watch was armed may never report; it is handled at once.
    void watch_exit(ChildState& child, pid_t pid, Tag tag) {
        const struct kevent watch = make_kevent(static_cast<std::uintptr_t>(pid), EVFILT_PROC, EV_ADD | EV_ONESHOT,
                                                NOTE_EXIT, 0, encode(child.id, tag));
        bool exited = false;
        if (::kevent(queue_.get(), &watch, 1, nullptr, 0, nullptr) != 0) {
            const int error = errno;
            exited = error == ESRCH || has_exited(static_cast<u32>(pid));
            if (!exited) REBOOT_LOG_ERROR(Engine, "the exit of child {} cannot be watched: errno {}", pid, error);
        } else {
            exited = has_exited(static_cast<u32>(pid));
        }
        if (exited) {
            if (tag == Tag::Child) {
                child_exited(child);
            } else {
                watchdog_exited(child);
            }
        }
    }

    void release_child(ChildState& child) {
        child.released = true;
        for (posix::UniqueFd& output : child.outputs) output.reset();
        forget_if_done(child);
    }

    void handle(const struct kevent& event) {
        if (event.filter == EVFILT_USER) return;
        const auto found = children_.find(id_of(event.udata));
        if (found == children_.end()) return;
        // Held, since forget_if_done may drop the map's reference while this runs.
        const std::shared_ptr<ChildState> state = found->second;
        ChildState& child = *state;
        switch (tag_of(event.udata)) {
            case Tag::Stdout:
            case Tag::Stderr:
                read_output(child, static_cast<std::size_t>(tag_of(event.udata)));
                break;
            case Tag::Stdin: {
                std::scoped_lock lock(child.stdin_mutex);
                if (!child.flush_stdin()) watch_stdin(child);
                break;
            }
            case Tag::Child:
                child_exited(child);
                break;
            case Tag::Watchdog:
                watchdog_exited(child);
                break;
            case Tag::Drain:
                child.drain_armed = false;
                for (posix::UniqueFd& output : child.outputs) output.reset();
                maybe_deliver_exit(child);
                break;
        }
    }

    void read_output(ChildState& child, std::size_t stream) {
        while (child.outputs[stream].valid()) {
            const ssize_t got = ::read(child.outputs[stream].get(), read_buffer_.data(), read_buffer_.size());
            if (got > 0) {
                if (!child.released)
                    child.deliver_output(stream, std::span<const u8>(read_buffer_.data(), static_cast<std::size_t>(got)), true);
                continue;
            }
            if (got < 0 && errno == EINTR) continue;
            if (got < 0 && errno == EAGAIN) return;
            child.outputs[stream].reset();
        }
        maybe_deliver_exit(child);
    }

    void child_exited(ChildState& child) {
        if (child.child_exited) return;
        child.child_exited = true;
        // A pid watchdog must be gone before the pid is reaped, or it could signal a reused pid.
        if (!child.group) {
            std::scoped_lock lock(child.reap_mutex);
            if (!child.watchdog_reaped) {
                ::kill(child.watchdog_pid, SIGKILL);
                return;
            }
        }
        reap_child(child);
    }

    void watchdog_exited(ChildState& child) {
        {
            std::scoped_lock lock(child.reap_mutex);
            if (child.watchdog_reaped) return;
            (void)reap(child.watchdog_pid);
            child.watchdog_reaped = true;
        }
        if (!child.group && child.child_exited) reap_child(child);
        forget_if_done(child);
    }

    void reap_child(ChildState& child) {
        {
            std::scoped_lock lock(child.reap_mutex);
            if (child.child_reaped) return;
            child.exit = decode_wait_status(reap(child.pid));
            child.child_reaped = true;
        }
        if (child.outputs[0].valid() || child.outputs[1].valid()) {
            const struct kevent drain =
                make_kevent(static_cast<std::uintptr_t>(child.id), EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0,
                            static_cast<std::intptr_t>(kOutputDrainBound.count()), encode(child.id, Tag::Drain));
            child.drain_armed = ::kevent(queue_.get(), &drain, 1, nullptr, 0, nullptr) == 0;
            if (!child.drain_armed)
                for (posix::UniqueFd& output : child.outputs) output.reset();
        }
        maybe_deliver_exit(child);
    }

    void maybe_deliver_exit(ChildState& child) {
        if (child.exit_delivered || !child.exit || child.outputs[0].valid() || child.outputs[1].valid()) return;
        child.exit_delivered = true;
        if (child.drain_armed) {
            const struct kevent cancel = make_kevent(static_cast<std::uintptr_t>(child.id), EVFILT_TIMER, EV_DELETE);
            (void)::kevent(queue_.get(), &cancel, 1, nullptr, 0, nullptr);
            child.drain_armed = false;
        }
        if (!child.released) child.deliver_exit(*child.exit, true);
        forget_if_done(child);
    }

    void forget_if_done(ChildState& child) {
        const bool reaped = [&] {
            std::scoped_lock lock(child.reap_mutex);
            return child.child_reaped && child.watchdog_reaped;
        }();
        if (reaped && (child.exit_delivered || child.released) && !child.drain_armed) children_.erase(child.id);
    }

    posix::UniqueFd queue_;
    std::thread thread_;
    std::atomic<std::thread::id> thread_id_{};
    std::mutex commands_mutex_;
    std::deque<Command> commands_;
    bool stopping_ = false;
    std::unordered_map<u64, std::shared_ptr<ChildState>> children_;
    // Allocated once: zeroing a chunk per read would cost more than the read.
    std::vector<u8> read_buffer_ = std::vector<u8>(kReadChunk);
};

class MacChild final : public ports::ChildProcess {
public:
    MacChild(std::shared_ptr<Loop> loop, std::shared_ptr<ChildState> state, std::chrono::system_clock::time_point created,
             posix::UniqueFd watchdog_pipe)
        : loop_(std::move(loop)), state_(std::move(state)), created_(created), watchdog_pipe_(std::move(watchdog_pipe)) {}

    ~MacChild() override {
        {
            std::scoped_lock lock(state_->output_mutex);
            state_->closed = true;
            state_->drop_callbacks(loop_->on_loop_thread());
        }
        {
            std::scoped_lock lock(state_->stdin_mutex);
            state_->outbox.clear();
            state_->stdin_fd.reset();
        }
        // EOF wakes the watchdog, which kills the group or the pid.
        watchdog_pipe_.reset();
        loop_->release(state_);
    }

    MacChild(const MacChild&) = delete;
    MacChild& operator=(const MacChild&) = delete;

    [[nodiscard]] u32 pid() const override { return static_cast<u32>(state_->pid); }
    [[nodiscard]] std::chrono::system_clock::time_point created() const override { return created_; }

    void write_stdin(std::span<const u8> bytes) override {
        if (!state_->control_channel || bytes.empty()) return;
        std::scoped_lock lock(state_->stdin_mutex);
        if (state_->stdin_closing || !state_->stdin_fd.valid()) return;
        state_->outbox.emplace_back(bytes.begin(), bytes.end());
        if (!state_->flush_stdin()) loop_->watch_stdin(*state_);
    }

    void close_stdin() override {
        std::scoped_lock lock(state_->stdin_mutex);
        state_->stdin_closing = true;
        if (state_->outbox.empty()) state_->stdin_fd.reset();
    }

    void on_stdout(OutputCallback callback) override { set_output(0, std::move(callback)); }
    void on_stderr(OutputCallback callback) override { set_output(1, std::move(callback)); }

    void on_exit(UniqueFunction<void(ports::ChildExit)> callback) override {
        std::scoped_lock lock(state_->output_mutex);
        if (state_->held_exit) {
            const ports::ChildExit exit = *state_->held_exit;
            state_->held_exit.reset();
            if (callback) callback(exit);
            return;
        }
        state_->on_exit = std::move(callback);
    }

    Result<void> terminate_tree() override {
        std::scoped_lock lock(state_->reap_mutex);
        if (state_->group) {
            // The group lives while its leader or the watchdog in it is unreaped.
            if (state_->child_reaped && state_->watchdog_reaped) return {};
            if (::kill(-state_->pid, SIGKILL) != 0 && errno != ESRCH) return std::unexpected(posix::call_failed("kill", errno));
            return {};
        }
        if (state_->child_reaped) return {};
        if (::kill(state_->pid, SIGKILL) != 0 && errno != ESRCH) return std::unexpected(posix::call_failed("kill", errno));
        return {};
    }

private:
    // Output that arrived before the callback is handed over first, in order.
    void set_output(std::size_t stream, OutputCallback callback) {
        std::scoped_lock lock(state_->output_mutex);
        if (!state_->held_output[stream].empty() && callback) {
            const Bytes held = std::move(state_->held_output[stream]);
            state_->held_output[stream].clear();
            callback(held);
        }
        state_->on_output[stream] = std::move(callback);
    }

    std::shared_ptr<Loop> loop_;
    std::shared_ptr<ChildState> state_;
    std::chrono::system_clock::time_point created_;
    posix::UniqueFd watchdog_pipe_;
};

}  // namespace

struct PosixSpawnLauncher::Impl {
    posix::PosixSpawner spawner{process_start_time};
    std::shared_ptr<Loop> loop = std::make_shared<Loop>();
    Result<void> started = loop->start();
    std::mutex spawn_mutex;
    u64 next_id = 1;
};

PosixSpawnLauncher::PosixSpawnLauncher() : impl_(std::make_unique<Impl>()) {}

PosixSpawnLauncher::~PosixSpawnLauncher() { impl_->loop->stop(); }

Result<std::unique_ptr<ports::ChildProcess>> PosixSpawnLauncher::spawn(const ports::ProcessLaunch& launch) {
    if (!impl_->started) return std::unexpected(impl_->started.error());
    Result<posix::SpawnedChild> child = impl_->spawner.spawn(launch);
    if (!child) return std::unexpected(std::move(child.error()));
    const auto pid = static_cast<pid_t>(child->pid);

    ports::ProcessLaunch watchdog;
    watchdog.exe = "/bin/sh";
    watchdog.args = {"-c", watchdog_script(launch.own_group ? std::nullopt : std::optional<u32>{child->pid})};
    watchdog.cwd = "/";
    watchdog.stdio = ports::StdioMode::Null;
    watchdog.own_group = true;
    Result<posix::SpawnedChild> guard = launch.own_group ? impl_->spawner.spawn_into_group(watchdog, child->pid)
                                                         : impl_->spawner.spawn(watchdog);
    if (!guard) {
        // A child without its watchdog would outlive the engine.
        ::kill(launch.own_group ? -pid : pid, SIGKILL);
        (void)reap(pid);
        return std::unexpected(std::move(guard.error()));
    }

    auto state = std::make_shared<ChildState>();
    {
        std::scoped_lock lock(impl_->spawn_mutex);
        state->id = impl_->next_id++;
    }
    state->pid = pid;
    state->watchdog_pid = static_cast<pid_t>(guard->pid);
    state->group = launch.own_group;
    state->control_channel = launch.stdio == ports::StdioMode::ControlChannel;
    state->stdin_fd = std::move(child->stdin_write);
    (void)set_nonblocking(state->stdin_fd.get());
    // F_SETNOSIGPIPE keeps a write to a dead child an EPIPE whatever the engine does with SIGPIPE.
    (void)::fcntl(state->stdin_fd.get(), F_SETNOSIGPIPE, 1);
    state->outputs = {std::move(child->stdout_read), std::move(child->stderr_read)};
    for (posix::UniqueFd& output : state->outputs)
        if (output.valid()) (void)set_nonblocking(output.get());

    auto process = std::make_unique<MacChild>(impl_->loop, state, child->created, std::move(guard->stdin_write));
    impl_->loop->add(std::move(state));
    return std::unique_ptr<ports::ChildProcess>{std::move(process)};
}

Result<bool> PosixSpawnLauncher::is_alive(u32 pid, std::chrono::system_clock::time_point created) {
    return impl_->spawner.is_alive(pid, created);
}

Result<void> PosixSpawnLauncher::kill(u32 pid, std::chrono::system_clock::time_point created) {
    return impl_->spawner.kill_tree(pid, created);
}

}  // namespace reboot::os_macos::platform
