#include "reboot/posix/posix_spawner.hpp"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <vector>

#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/process_start_time.hpp"
#include "unistd.hpp"

namespace reboot::posix {

namespace {

struct Pipe {
    UniqueFd read;
    UniqueFd write;
};

// Close-on-exec, so a process another thread spawns meanwhile cannot hold a child's stdin open;
// the dup2 file actions clear the flag on the child's copies.
[[nodiscard]] Result<Pipe> make_pipe() {
    std::array<int, 2> ends{-1, -1};
    if (::pipe(ends.data()) != 0) return std::unexpected(call_failed("pipe", errno));
    Pipe pipe{UniqueFd{ends[0]}, UniqueFd{ends[1]}};
    for (const int end : ends)
        if (::fcntl(end, F_SETFD, FD_CLOEXEC) != 0) return std::unexpected(call_failed("fcntl", errno));
    return pipe;
}

class SpawnFileActions {
public:
    SpawnFileActions() noexcept { error_ = ::posix_spawn_file_actions_init(&actions_); }
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
    SpawnAttributes() noexcept { error_ = ::posix_spawnattr_init(&attributes_); }
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

// Keeps the first non-zero return of the posix_spawn_* setup calls.
class FirstError {
public:
    void operator()(int code) noexcept {
        if (code_ == 0) code_ = code;
    }
    [[nodiscard]] int code() const noexcept { return code_; }

private:
    int code_ = 0;
};

void kill_and_reap(pid_t pid) noexcept {
    ::kill(pid, SIGKILL);
    while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
    }
}

}  // namespace

Result<SpawnedChild> PosixSpawner::spawn(const ports::ProcessLaunch& launch) {
    return spawn_with_group(launch, std::nullopt);
}

Result<SpawnedChild> PosixSpawner::spawn_into_group(const ports::ProcessLaunch& launch, u32 pgid) {
    return spawn_with_group(launch, pgid);
}

Result<SpawnedChild> PosixSpawner::spawn_with_group(const ports::ProcessLaunch& launch, std::optional<u32> pgid) {
    std::vector<std::string> arg_storage;
    arg_storage.reserve(launch.args.size() + 1);
    arg_storage.push_back(launch.exe.native());
    arg_storage.insert(arg_storage.end(), launch.args.begin(), launch.args.end());
    std::vector<char*> argv;
    argv.reserve(arg_storage.size() + 1);
    for (std::string& arg : arg_storage) argv.push_back(arg.data());
    argv.push_back(nullptr);

    std::vector<std::string> env_storage;
    env_storage.reserve(launch.env.vars.size());
    for (const auto& [name, value] : launch.env.vars) env_storage.push_back(name + "=" + value);
    std::vector<char*> envp;
    envp.reserve(env_storage.size() + 1);
    for (std::string& entry : env_storage) envp.push_back(entry.data());
    envp.push_back(nullptr);

    auto stdin_pipe = make_pipe();
    if (!stdin_pipe) return std::unexpected(std::move(stdin_pipe.error()));
    std::optional<Pipe> stdout_pipe;
    std::optional<Pipe> stderr_pipe;
    if (launch.stdio != ports::StdioMode::Null) {
        auto out = make_pipe();
        if (!out) return std::unexpected(std::move(out.error()));
        auto err = make_pipe();
        if (!err) return std::unexpected(std::move(err.error()));
        stdout_pipe.emplace(std::move(*out));
        stderr_pipe.emplace(std::move(*err));
    }

    SpawnFileActions actions;
    SpawnAttributes attributes;
    FirstError setup;
    setup(actions.error());
    setup(attributes.error());
    if (setup.code() != 0) return std::unexpected(call_failed("posix_spawn", setup.code(), launch.exe));

    setup(::posix_spawn_file_actions_adddup2(actions.get(), stdin_pipe->read.get(), STDIN_FILENO));
    if (stdout_pipe) {
        setup(::posix_spawn_file_actions_adddup2(actions.get(), stdout_pipe->write.get(), STDOUT_FILENO));
        setup(::posix_spawn_file_actions_adddup2(actions.get(), stderr_pipe->write.get(), STDERR_FILENO));
    } else {
        setup(::posix_spawn_file_actions_addopen(actions.get(), STDOUT_FILENO, "/dev/null", O_WRONLY, 0));
        setup(::posix_spawn_file_actions_addopen(actions.get(), STDERR_FILENO, "/dev/null", O_WRONLY, 0));
    }
    if (!launch.cwd.empty()) setup(::posix_spawn_file_actions_addchdir_np(actions.get(), launch.cwd.c_str()));

    sigset_t empty_mask;
    sigemptyset(&empty_mask);
    sigset_t defaults;
    sigemptyset(&defaults);
    for (int number = 1; number < NSIG; ++number)
        if (number != SIGKILL && number != SIGSTOP) sigaddset(&defaults, number);
    setup(::posix_spawnattr_setsigmask(attributes.get(), &empty_mask));
    setup(::posix_spawnattr_setsigdefault(attributes.get(), &defaults));
    int flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_CLOEXEC_DEFAULT;
    if (pgid || launch.own_group) {
        flags |= POSIX_SPAWN_SETPGROUP;
        setup(::posix_spawnattr_setpgroup(attributes.get(), pgid ? static_cast<pid_t>(*pgid) : 0));
    }
    setup(::posix_spawnattr_setflags(attributes.get(), static_cast<short>(flags)));
    if (setup.code() != 0) return std::unexpected(call_failed("posix_spawn", setup.code(), launch.exe));

    pid_t pid = 0;
    if (const int error = ::posix_spawn(&pid, launch.exe.c_str(), actions.get(), attributes.get(), argv.data(),
                                        envp.data());
        error != 0)
        return std::unexpected(call_failed("posix_spawn", error, launch.exe));

    auto created = read_start_time_(static_cast<u32>(pid));
    if (!created || !*created) {
        kill_and_reap(pid);
        if (!created) return std::unexpected(std::move(created.error()));
        return std::unexpected(call_failed("posix_spawn", ESRCH, launch.exe));
    }

    SpawnedChild child;
    child.pid = static_cast<u32>(pid);
    child.created = **created;
    child.stdin_write = std::move(stdin_pipe->write);
    if (stdout_pipe) {
        child.stdout_read = std::move(stdout_pipe->read);
        child.stderr_read = std::move(stderr_pipe->read);
    }
    return child;
}

Result<bool> PosixSpawner::is_alive(u32 pid, std::chrono::system_clock::time_point created) {
    auto started = read_start_time_(pid);
    if (!started) return std::unexpected(std::move(started.error()));
    return started->has_value() && same_start_time(created, **started);
}

Result<void> PosixSpawner::kill_tree(u32 pid, std::chrono::system_clock::time_point created) {
    auto alive = is_alive(pid, created);
    if (!alive) return std::unexpected(std::move(alive.error()));
    if (!*alive) return {};
    const auto target = static_cast<pid_t>(pid);
    const pid_t group = ::getpgid(target);
    if (group < 0) {
        if (errno == ESRCH) return {};
        return std::unexpected(call_failed("getpgid", errno));
    }
    if (::kill(group == target ? -target : target, SIGKILL) != 0 && errno != ESRCH)
        return std::unexpected(call_failed("kill", errno));
    return {};
}

}  // namespace reboot::posix
