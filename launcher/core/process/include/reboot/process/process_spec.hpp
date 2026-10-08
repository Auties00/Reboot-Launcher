#pragma once

#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/process/built_env.hpp"
#include "reboot/process/child_record.hpp"
#include "reboot/process/wiping_launch.hpp"

namespace reboot::process {

// Capabilities: game-launch.process-spawn-helper (suspended creation is ISessionHost's, not this spec's).
// A native child as the engine starts it: the real program with an argument vector, never a
// shell or batch wrapper, so the tracked pid is the program itself.
struct ProcessSpec {
    ChildRole role{};
    std::optional<SessionId> session;
    NativePath exe;
    std::vector<std::string> args;
    BuiltEnv env;
    // Absent: the exe's directory (process-model). Set only where the child's contract names
    // another, such as the backend's data directory.
    std::optional<NativePath> cwd;
    ports::StdioMode stdio = ports::StdioMode::ControlChannel;
    // Linux: the per-session systemd user scope, when systemd is available.
    std::optional<std::string> scope_name;

    [[nodiscard]] NativePath working_directory() const;
    // Absolute exe and cwd; no empty argument; no NUL or invalid UTF-8 in an argument.
    [[nodiscard]] Result<void> validate() const;
    // Always its own process group.
    [[nodiscard]] WipingLaunch to_launch() const;
};

}  // namespace reboot::process
