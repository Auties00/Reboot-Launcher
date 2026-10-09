#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::os_linux::platform {

// Every helper program (xdg-mime, systemctl, loginctl, python3, xdg-open) is bounded by this.
inline constexpr std::chrono::seconds kHelperDeadline{5};

enum class OnTimeout : u8 {
    // SIGKILL and reap it, then fail with platform.helper_timeout.
    Kill,
    // Leave it running and reaped in the background, and report it as started; for openers that
    // may stay in the foreground as long as what they opened.
    Detach,
};

struct HelperCommand {
    // A bare name is looked up on this process's PATH.
    std::string program;
    std::vector<std::string> args;
    // This process's environment when unset.
    std::optional<std::vector<std::pair<std::string, std::string>>> env;
    bool capture_stdout = false;
    OnTimeout on_timeout = OnTimeout::Kill;
};

struct HelperResult {
    // nullopt when a signal ended it, or when it was detached still running.
    std::optional<int> exit_code;
    bool detached = false;
    // Only with capture_stdout; capped at 1 MiB.
    std::string output;
};

// posix_spawnp with stdin and stderr on /dev/null, default signal dispositions and an empty mask.
// A program that cannot start fails with posix.call_failed (ErrorKind::NotFound when missing).
[[nodiscard]] Result<HelperResult> run_helper(const HelperCommand& command,
                                              std::chrono::milliseconds deadline = kHelperDeadline);

// The first executable regular file named `name` in the colon-separated `path_list`.
[[nodiscard]] std::optional<NativePath> find_in_path(std::string_view name, std::string_view path_list);

// platform.helper_failed for a helper that ran but did not exit 0.
[[nodiscard]] Diagnostic helper_failed(std::string_view program, const HelperResult& result);

}  // namespace rb::os_linux::platform
