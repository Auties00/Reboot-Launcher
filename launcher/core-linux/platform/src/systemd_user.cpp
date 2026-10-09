#include "systemd_user.hpp"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <utility>

#include "process_environment.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace rb::os_linux::platform {

bool unix_socket_accepts(const NativePath& path) {
    sockaddr_un address{};
    if (path.native().size() + 1 > sizeof address.sun_path) return false;
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.native().size() + 1);
    const posix::UniqueFd fd{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (!fd.valid()) return false;
    for (;;) {
        if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0) return true;
        if (errno != EINTR) return false;
    }
}

bool systemd_user_manager_answers(const NativePath& runtime_dir) {
    return unix_socket_accepts(runtime_dir / "systemd" / "private");
}

Result<HelperResult> systemctl_user(std::vector<std::string> args, const NativePath& runtime_dir) {
    HelperCommand command;
    command.program = "systemctl";
    command.args = {"--user"};
    command.args.insert(command.args.end(), std::make_move_iterator(args.begin()), std::make_move_iterator(args.end()));
    auto env = current_environment();
    env.emplace_back("XDG_RUNTIME_DIR", runtime_dir.native());
    command.env = std::move(env);
    command.capture_stdout = true;
    return run_helper(command);
}

Result<void> systemctl_user_ok(std::vector<std::string> args, const NativePath& runtime_dir) {
    Result<HelperResult> ran = systemctl_user(std::move(args), runtime_dir);
    if (!ran) return std::unexpected(std::move(ran.error()));
    if (ran->exit_code != 0) return std::unexpected(helper_failed("systemctl", *ran));
    return {};
}

}  // namespace rb::os_linux::platform
