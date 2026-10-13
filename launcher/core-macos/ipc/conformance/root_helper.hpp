#pragma once

#include <sys/types.h>

#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "reboot/base/diag.hpp"
#include "reboot/base/native_path.hpp"
#include "reboot/base/types.hpp"
#include "reboot/posix/unique_fd.hpp"

// Another user for the IPC suite: this executable re-run as root through `sudo -n`, which the
// GitHub macOS runners allow without a password.
namespace rb::os_macos::ipc {

inline constexpr std::string_view kRootHelperFlag = "--root-helper";

// Whether `sudo -n` runs a command as root; never when this process is root already.
[[nodiscard]] bool passwordless_sudo();

// A socket the root helper holds at `socket_path` until this is destroyed.
class RootSocket {
public:
    enum class Mode : u8 { Connect, Squat };

    // Returns once the helper has connected (Connect) or is listening (Squat); its errno otherwise.
    [[nodiscard]] static Result<std::unique_ptr<RootSocket>> open(Mode mode, const NativePath& socket_path);

    // Closes the helper's stdin, which ends it, and reaps it.
    ~RootSocket();
    RootSocket(const RootSocket&) = delete;
    RootSocket& operator=(const RootSocket&) = delete;

    [[nodiscard]] pid_t pid() const noexcept { return pid_; }
    // Relayed by a Connect helper to its socket; nothing comes back.
    void send(std::span<const u8> bytes);

private:
    RootSocket(pid_t pid, posix::UniqueFd to_helper) noexcept : pid_(pid), to_helper_(std::move(to_helper)) {}

    pid_t pid_;
    posix::UniqueFd to_helper_;
};

// The helper's main, run as root: <exe> --root-helper connect|squat <socket path>.
[[nodiscard]] int run_root_helper(int argc, char* argv[]);

}  // namespace rb::os_macos::ipc
