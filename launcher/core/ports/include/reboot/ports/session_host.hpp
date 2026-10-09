#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/process.hpp"

namespace rb::ports {

using BootStrategy = contracts::winhost::BootStrategy;
using InjectPhase = contracts::winhost::InjectPhase;
using OutputStream = contracts::winhost::OutputStream;

struct InjectEntry {
    NativePath path;
    std::array<u8, 32> sha256{};
    BootStrategy strategy{};
    InjectPhase phase{};
};

struct CompanionSpec {
    NativePath exe;
    std::vector<std::string> args;
};

struct SessionLaunch {
    SessionId session;
    NativePath exe;
    std::vector<std::string> args;
    EnvBlock env;
    NativePath cwd;
    std::vector<CompanionSpec> companions;
    std::vector<InjectEntry> inject;
    // Renamed aside before the spawn and restored when the session ends; a file that is missing
    // or cannot be renamed is skipped.
    std::vector<NativePath> park;
    RunnerMultiplier multiplier = RunnerMultiplier::Native;
};

enum class SessionRole : u8 { Game, Companion, Winhost };

struct Spawned {
    SessionRole role{};
    u32 pid = 0;
};

// `error` is a GuestWindows code under Wine.
struct Injected {
    NativePath path;
    bool ok = false;
    std::optional<SystemError> error;
};

struct Output {
    SessionRole role{};
    OutputStream stream{};
    std::vector<u8> bytes;
};

struct Exited {
    SessionRole role{};
    std::optional<int> code;
};

struct HostFatal {
    Diagnostic error;
};

using SessionHostEvent = std::variant<Spawned, Injected, Output, Exited, HostFatal>;

// Ends the whole session tree on destruction.
class IGameSession {
public:
    virtual ~IGameSession() = default;

    virtual Result<void> inject(const InjectEntry& entry) = 0;
    virtual Result<void> resume() = 0;
    virtual void stop(std::chrono::milliseconds grace) = 0;
};

// Launches the game suspended with its companions and Early injections applied; the caller
// resumes. `on_event` runs on the I/O thread.
class ISessionHost {
public:
    virtual ~ISessionHost() = default;

    virtual Result<std::unique_ptr<IGameSession>> launch(const SessionLaunch& launch,
                                                         UniqueFunction<void(SessionHostEvent)> on_event) = 0;
};

}  // namespace rb::ports
