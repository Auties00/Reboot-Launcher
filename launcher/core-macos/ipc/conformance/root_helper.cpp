#include "unistd.hpp"

#include "root_helper.hpp"

#include <crt_externs.h>
#include <fcntl.h>
#include <libproc.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "reboot/posix/posix_error.hpp"

namespace rb::os_macos::ipc {
namespace {

constexpr const char* kSudo = "/usr/bin/sudo";
// sudo itself, then the helper's connect or bind.
constexpr int kHelperReadyTimeoutMs = 10000;

[[nodiscard]] int wait_for(pid_t pid) noexcept {
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return status;
}

class SpawnSetup {
public:
    SpawnSetup() noexcept {
        actions_ok_ = ::posix_spawn_file_actions_init(&actions_) == 0;
        attributes_ok_ = ::posix_spawnattr_init(&attributes_) == 0;
    }
    ~SpawnSetup() {
        if (actions_ok_) ::posix_spawn_file_actions_destroy(&actions_);
        if (attributes_ok_) ::posix_spawnattr_destroy(&attributes_);
    }
    SpawnSetup(const SpawnSetup&) = delete;
    SpawnSetup& operator=(const SpawnSetup&) = delete;

    // stdin and stdout from `in` and `out` (/dev/null when negative), stderr inherited, nothing else.
    [[nodiscard]] bool configure(int in, int out) noexcept {
        if (!actions_ok_ || !attributes_ok_) return false;
        const int results[] = {
            in >= 0 ? ::posix_spawn_file_actions_adddup2(&actions_, in, STDIN_FILENO)
                    : ::posix_spawn_file_actions_addopen(&actions_, STDIN_FILENO, "/dev/null", O_RDONLY, 0),
            out >= 0 ? ::posix_spawn_file_actions_adddup2(&actions_, out, STDOUT_FILENO)
                     : ::posix_spawn_file_actions_addopen(&actions_, STDOUT_FILENO, "/dev/null", O_WRONLY, 0),
            ::posix_spawn_file_actions_addinherit_np(&actions_, STDERR_FILENO),
            ::posix_spawnattr_setflags(&attributes_, static_cast<short>(POSIX_SPAWN_CLOEXEC_DEFAULT)),
        };
        for (const int result : results)
            if (result != 0) return false;
        return true;
    }

    [[nodiscard]] std::optional<pid_t> spawn(std::vector<std::string> arguments) noexcept {
        std::vector<char*> argv;
        for (std::string& argument : arguments) argv.push_back(argument.data());
        argv.push_back(nullptr);
        pid_t pid = 0;
        if (::posix_spawn(&pid, kSudo, &actions_, &attributes_, argv.data(), *::_NSGetEnviron()) != 0) return std::nullopt;
        return pid;
    }

private:
    posix_spawn_file_actions_t actions_{};
    posix_spawnattr_t attributes_{};
    bool actions_ok_ = false;
    bool attributes_ok_ = false;
};

[[nodiscard]] std::optional<std::string> own_executable() {
    std::array<char, PROC_PIDPATHINFO_MAXSIZE> path{};
    const int length = ::proc_pidpath(::getpid(), path.data(), static_cast<u32>(path.size()));
    if (length <= 0) return std::nullopt;
    return std::string(path.data(), static_cast<std::size_t>(length));
}

// The helper's status: 0 once its socket is ready, its errno otherwise; nullopt when it never answers.
[[nodiscard]] std::optional<int> read_status(int from_helper) {
    std::array<u8, sizeof(int)> bytes{};
    std::size_t got = 0;
    while (got < bytes.size()) {
        pollfd ready{.fd = from_helper, .events = POLLIN, .revents = 0};
        const int polled = ::poll(&ready, 1, kHelperReadyTimeoutMs);
        if (polled < 0 && errno == EINTR) continue;
        if (polled <= 0) return std::nullopt;
        const ssize_t count = ::read(from_helper, bytes.data() + got, bytes.size() - got);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return std::nullopt;
        got += static_cast<std::size_t>(count);
    }
    int status = 0;
    std::memcpy(&status, bytes.data(), sizeof status);
    return status;
}

void write_all(int fd, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const char*>(data);
    while (size > 0) {
        const ssize_t written = ::write(fd, bytes, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return;
        bytes += written;
        size -= static_cast<std::size_t>(written);
    }
}

}  // namespace

bool passwordless_sudo() {
    if (::geteuid() == 0) return false;
    SpawnSetup setup;
    if (!setup.configure(-1, -1)) return false;
    const std::optional<pid_t> pid = setup.spawn({kSudo, "-n", "/usr/bin/true"});
    if (!pid) return false;
    const int status = wait_for(*pid);
    return status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

Result<std::unique_ptr<RootSocket>> RootSocket::open(Mode mode, const NativePath& socket_path) {
    const std::optional<std::string> self = own_executable();
    if (!self) return std::unexpected(posix::call_failed("proc_pidpath", errno));
    std::array<int, 2> to_helper{-1, -1};
    std::array<int, 2> from_helper{-1, -1};
    if (::pipe(to_helper.data()) != 0) return std::unexpected(posix::call_failed("pipe", errno));
    posix::UniqueFd helper_in{to_helper[0]};
    posix::UniqueFd to_helper_write{to_helper[1]};
    if (::pipe(from_helper.data()) != 0) return std::unexpected(posix::call_failed("pipe", errno));
    posix::UniqueFd from_helper_read{from_helper[0]};
    posix::UniqueFd helper_out{from_helper[1]};

    SpawnSetup setup;
    if (!setup.configure(helper_in.get(), helper_out.get())) return std::unexpected(posix::call_failed("posix_spawn", EINVAL));
    const std::optional<pid_t> pid = setup.spawn({kSudo, "-n", *self, std::string(kRootHelperFlag),
                                                  mode == Mode::Connect ? "connect" : "squat", socket_path.native()});
    if (!pid) return std::unexpected(posix::call_failed("posix_spawn", errno, NativePath{kSudo}));
    helper_in.reset();
    helper_out.reset();

    const std::optional<int> status = read_status(from_helper_read.get());
    if (status == 0) {
        // A relay to a helper that has exited fails with EPIPE rather than killing the suite.
        (void)::fcntl(to_helper_write.get(), F_SETNOSIGPIPE, 1);
        return std::unique_ptr<RootSocket>(new RootSocket(*pid, std::move(to_helper_write)));
    }
    // Closing its stdin ends a helper that answered late; sudo runs as root, so the kill may not land.
    to_helper_write.reset();
    ::kill(*pid, SIGKILL);
    (void)wait_for(*pid);
    if (!status) return std::unexpected(posix::call_failed("sudo", ETIMEDOUT, NativePath{kSudo}));
    return std::unexpected(posix::call_failed(mode == Mode::Connect ? "connect" : "bind", *status, socket_path));
}

RootSocket::~RootSocket() {
    to_helper_.reset();
    (void)wait_for(pid_);
}

void RootSocket::send(std::span<const u8> bytes) {
    if (to_helper_.valid()) write_all(to_helper_.get(), bytes.data(), bytes.size());
}

int run_root_helper(int argc, char* argv[]) {
    if (argc != 4) return 2;
    const std::string_view mode = argv[2];
    const bool connecting = mode == "connect";
    if (!connecting && mode != "squat") return 2;
    // A Connect helper outlives the listener that drops it, and its relay must not end it.
    ::signal(SIGPIPE, SIG_IGN);

    const std::string_view path = argv[3];
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    int error = 0;
    posix::UniqueFd endpoint{::socket(AF_UNIX, SOCK_STREAM, 0)};
    if (path.size() >= sizeof address.sun_path) {
        error = ENAMETOOLONG;
    } else if (!endpoint.valid()) {
        error = errno;
    } else {
        std::memcpy(address.sun_path, path.data(), path.size());
        const auto* target = reinterpret_cast<const sockaddr*>(&address);
        // A squatter's socket is open to all, or the user's connect would stop at EACCES.
        const bool ready = connecting ? ::connect(endpoint.get(), target, sizeof address) == 0
                                      : ::bind(endpoint.get(), target, sizeof address) == 0 &&
                                            ::chmod(argv[3], 0777) == 0 && ::listen(endpoint.get(), 4) == 0;
        if (!ready) error = errno;
    }
    write_all(STDOUT_FILENO, &error, sizeof error);
    if (error != 0) return 1;

    std::array<char, 4096> buffer{};
    for (;;) {
        const ssize_t got = ::read(STDIN_FILENO, buffer.data(), buffer.size());
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        if (connecting) (void)::send(endpoint.get(), buffer.data(), static_cast<std::size_t>(got), 0);
    }
    if (!connecting) ::unlink(argv[3]);
    return 0;
}

}  // namespace rb::os_macos::ipc
