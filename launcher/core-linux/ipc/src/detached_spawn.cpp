#include "detached_spawn.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "messages.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace rb::os_linux::ipc {
namespace {

// Without close_range and under an unlimited RLIMIT_NOFILE, the close loop stops here.
constexpr int kFallbackFdLimit = 1 << 16;
// EL8's kernel headers predate close_range; its number is the same on every architecture we build.
#if defined(SYS_close_range)
constexpr long kSysCloseRange = SYS_close_range;
#else
constexpr long kSysCloseRange = 436;
#endif

// Everything the children use, built before fork.
struct ChildPlan {
    const char* program = nullptr;
    char* const* argv = nullptr;
    char* const* envp = nullptr;
    const char* cwd = nullptr;
    int error_fd = -1;
    int fd_limit = kFallbackFdLimit;
    sigset_t empty_mask{};
    struct sigaction default_action {};
};

[[nodiscard]] std::vector<char*> c_strings(const std::vector<std::string>& strings) {
    std::vector<char*> pointers;
    pointers.reserve(strings.size() + 1);
    for (const std::string& text : strings) pointers.push_back(const_cast<char*>(text.c_str()));
    pointers.push_back(nullptr);
    return pointers;
}

[[noreturn]] void fail_child(int error_fd, int error) noexcept {
    while (::write(error_fd, &error, sizeof error) < 0 && errno == EINTR) {
    }
    ::_exit(127);
}

void close_inherited_fds(int keep, int limit) noexcept {
    const bool below_closed = keep == 3 || ::syscall(kSysCloseRange, 3U, static_cast<unsigned>(keep - 1), 0U) == 0;
    if (below_closed && ::syscall(kSysCloseRange, static_cast<unsigned>(keep + 1), ~0U, 0U) == 0) return;
    for (int fd = 3; fd < limit; ++fd)
        if (fd != keep) ::close(fd);
}

[[noreturn]] void run_grandchild(const ChildPlan& plan) noexcept {
    // An ignored signal survives exec, so the host app's dispositions would reach the engine.
    for (int number = 1; number < NSIG; ++number)
        if (number != SIGKILL && number != SIGSTOP) ::sigaction(number, &plan.default_action, nullptr);
    ::sigprocmask(SIG_SETMASK, &plan.empty_mask, nullptr);
    close_inherited_fds(plan.error_fd, plan.fd_limit);
    const int null_fd = ::open("/dev/null", O_RDWR);
    if (null_fd < 0) fail_child(plan.error_fd, errno);
    for (int target = STDIN_FILENO; target <= STDERR_FILENO; ++target)
        if (null_fd != target && ::dup2(null_fd, target) < 0) fail_child(plan.error_fd, errno);
    if (null_fd > STDERR_FILENO) ::close(null_fd);
    if (::chdir(plan.cwd) != 0) fail_child(plan.error_fd, errno);
    ::execve(plan.program, plan.argv, plan.envp);
    fail_child(plan.error_fd, errno);
}

[[noreturn]] void run_intermediate(const ChildPlan& plan) noexcept {
    if (::setsid() < 0) fail_child(plan.error_fd, errno);
    // _Fork skips the atfork handlers, which are not async-signal-safe in a forked child.
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    const pid_t grandchild = ::_Fork();
#else
    const pid_t grandchild = ::fork();
#endif
    if (grandchild < 0) fail_child(plan.error_fd, errno);
    if (grandchild > 0) ::_exit(0);
    run_grandchild(plan);
}

[[nodiscard]] int open_fd_limit() noexcept {
    rlimit limit{};
    if (::getrlimit(RLIMIT_NOFILE, &limit) != 0 || limit.rlim_cur == RLIM_INFINITY) return kFallbackFdLimit;
    return static_cast<int>(std::min<rlim_t>(limit.rlim_cur, INT_MAX));
}

}  // namespace

Result<void> spawn_detached(const DetachedLaunch& launch) {
    const auto failed = [&launch](int error) {
        return make_diag(ErrorDomain::Platform, kEngineSpawnFailed)
            .arg("path", launch.program)
            .os(posix::errno_error(error))
            .fail();
    };
    const std::vector<char*> argv = c_strings(launch.argv);
    const std::vector<char*> envp = c_strings(launch.envp);

    std::array<int, 2> ends{-1, -1};
    if (::pipe2(ends.data(), O_CLOEXEC) != 0) return failed(errno);
    const posix::UniqueFd read_end{ends[0]};
    posix::UniqueFd write_end{ends[1]};
    // The grandchild's stdio would take the numbers 0 to 2.
    if (write_end.get() <= STDERR_FILENO)
        write_end.reset(::fcntl(write_end.get(), F_DUPFD_CLOEXEC, STDERR_FILENO + 1));
    if (!write_end.valid()) return failed(errno);

    ChildPlan plan{
        .program = launch.program.c_str(),
        .argv = argv.data(),
        .envp = envp.data(),
        .cwd = launch.cwd.c_str(),
        .error_fd = write_end.get(),
        .fd_limit = open_fd_limit(),
    };
    ::sigemptyset(&plan.empty_mask);
    plan.default_action.sa_handler = SIG_DFL;
    ::sigemptyset(&plan.default_action.sa_mask);

    const pid_t child = ::fork();
    if (child < 0) return failed(errno);
    if (child == 0) run_intermediate(plan);
    write_end.reset();
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }

    // End of stream once the exec closed the last write end; an errno otherwise.
    int error = 0;
    std::size_t received = 0;
    while (received < sizeof error) {
        char* const into = reinterpret_cast<char*>(&error) + received;
        const ssize_t got = ::read(read_end.get(), into, sizeof error - received);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        received += static_cast<std::size_t>(got);
    }
    if (received == 0) return {};
    return failed(received == sizeof error ? error : EIO);
}

}  // namespace rb::os_linux::ipc
