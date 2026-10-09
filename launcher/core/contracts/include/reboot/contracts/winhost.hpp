#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

// reboot-winhost.exe inside the Wine prefix, over loopback TCP to the engine's game channel,
// after the game-control preamble. Windows strings are UTF-16LE bytes. Requests carry req_id
// and get a common::CommandResult. On EOF winhost terminates its Job and exits.
namespace rb::contracts::winhost {

using Bytes = std::vector<u8>;

inline constexpr u32 kWinhostProtocol = VersionStreams::winhost_protocol;

enum class BootStrategy : u8 { EarlyBirdApc, AfterResume };
enum class InjectPhase : u8 { Early, LoggedIn };
enum class ProcessRole : u8 { Game, Companion };
enum class OutputStream : u8 { Stdout, Stderr };

// The token comes from winhost's environment and is compared in constant time.
struct WhHello {
    std::array<u8, 32> token{};
    std::string build;
    u32 protocol = 0;
    u32 pid = 0;
};
REBOOT_CONTRACT_FRAME(WhHello, 0x500)

struct InjectSpec {
    Bytes path_utf16;
    std::array<u8, 32> sha256{};
    BootStrategy strategy{};
    InjectPhase phase{};
};

struct CompanionSpawn {
    Bytes exe_utf16;
    std::vector<Bytes> argv_utf16;
};

// `env_block_utf16` is a complete Windows environment block, double-NUL terminated.
struct SpawnGame {
    Bytes exe_utf16;
    std::vector<Bytes> argv_utf16;
    Bytes env_block_utf16;
    Bytes cwd_utf16;
    std::vector<CompanionSpawn> companions;
    std::vector<InjectSpec> inject;
    // ports::SessionLaunch::park, mapped into the prefix.
    std::vector<Bytes> park_utf16;
    // Session wait bounds, already scaled by the engine for the runner: one DLL load (a waited
    // remote load, or an early-bird load showing up after resume), and the killed Job emptying.
    u32 inject_timeout_ms = 0;
    u32 drain_timeout_ms = 0;
};

struct WhWelcome {
    SpawnGame spawn;
};
REBOOT_CONTRACT_FRAME(WhWelcome, 0x501)

// winhost -> engine events

struct Spawned {
    ProcessRole role{};
    u32 pid = 0;
};
REBOOT_CONTRACT_FRAME(Spawned, 0x510)

// `error` is a Windows error code from inside the prefix.
struct Injected {
    Bytes path_utf16;
    bool ok = false;
    std::optional<i64> error;
};
REBOOT_CONTRACT_FRAME(Injected, 0x511)

struct Output {
    ProcessRole role{};
    OutputStream stream{};
    Bytes bytes;
};
REBOOT_CONTRACT_FRAME(Output, 0x512)

struct Exited {
    ProcessRole role{};
    std::optional<i64> code;
};
REBOOT_CONTRACT_FRAME(Exited, 0x513)

struct WhFatal {
    std::string step;
    std::optional<i64> os_code;
};
REBOOT_CONTRACT_FRAME(WhFatal, 0x514)

// engine -> winhost requests

struct Resume {
    u64 req_id = 0;
};
REBOOT_CONTRACT_FRAME(Resume, 0x520)

struct Inject {
    u64 req_id = 0;
    InjectSpec entry;
};
REBOOT_CONTRACT_FRAME(Inject, 0x521)

struct Stop {
    u64 req_id = 0;
    u32 grace_ms = 0;
};
REBOOT_CONTRACT_FRAME(Stop, 0x522)

}  // namespace rb::contracts::winhost
