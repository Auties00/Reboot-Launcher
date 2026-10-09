#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::ports {

// UTF-8; the launcher converts to the OS form.
struct EnvBlock {
    std::vector<std::pair<std::string, std::string>> vars;
};

// ControlChannel: framed stdin/stdout plus line-read stderr. Capture: stdout and stderr only.
enum class StdioMode : u8 { ControlChannel, Capture, Null };

struct ProcessLaunch {
    NativePath exe;
    std::vector<std::string> args;
    EnvBlock env;
    NativePath cwd;
    StdioMode stdio = StdioMode::Null;
    bool own_group = true;
    // Linux: the systemd user scope to run under, when systemd is available.
    std::optional<std::string> scope_name;
};

struct ChildExit {
    std::optional<int> code;
    std::optional<int> signal;
};

// Callbacks run on the I/O thread and must only post to the strand.
class ChildProcess {
public:
    virtual ~ChildProcess() = default;

    [[nodiscard]] virtual u32 pid() const = 0;
    [[nodiscard]] virtual std::chrono::system_clock::time_point created() const = 0;

    virtual void write_stdin(std::span<const u8> bytes) = 0;
    virtual void close_stdin() = 0;
    virtual void on_stdout(UniqueFunction<void(std::span<const u8>)> callback) = 0;
    virtual void on_stderr(UniqueFunction<void(std::span<const u8>)> callback) = 0;
    virtual void on_exit(UniqueFunction<void(ChildExit)> callback) = 0;
    virtual Result<void> terminate_tree() = 0;
};

// Every child dies with the engine: a Job on Windows, a process group with a stdin-EOF watchdog
// on macOS and Linux, plus PDEATHSIG on Linux.
class IProcessLauncher {
public:
    virtual ~IProcessLauncher() = default;

    virtual Result<std::unique_ptr<ChildProcess>> spawn(const ProcessLaunch& launch) = 0;
    // `created` guards against pid reuse when reaping from runtime.json.
    virtual Result<bool> is_alive(u32 pid, std::chrono::system_clock::time_point created) = 0;
    virtual Result<void> kill(u32 pid, std::chrono::system_clock::time_point created) = 0;
};

}  // namespace rb::ports
