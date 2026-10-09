#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

// Our in-game client DLL over loopback TCP to the engine's game channel, after the
// game-control preamble. Requests carry req_id and get a common::CommandResult.
namespace reboot::contracts::game_client {

inline constexpr u16 kPayloadAbi = VersionStreams::payload_abi;

// Bootstrap environment, set in the game's Windows environment block; the DLL erases it.
inline constexpr std::string_view kEnvCtl = "REBOOT_CTL";
inline constexpr std::string_view kEnvCtlToken = "REBOOT_CTL_TOKEN";
inline constexpr std::string_view kEnvSession = "REBOOT_SESSION";
inline constexpr std::string_view kEnvRole = "REBOOT_ROLE";

enum class PeerRole : u8 { ClientDll, Winhost };
enum class BuildMatch : u8 { Exact, Family, None };
enum class PatchStatus : u8 { Applied, Skipped, NotFound, Failed };
enum class ExitKind : u8 { RequestExit, FatalError, Crash };

struct GameBuild {
    std::string version;
    u32 cl = 0;
};

// Must arrive within 2 s x the runner multiplier. The token is compared in constant time.
struct GcHello {
    std::array<u8, 32> token{};
    PeerRole role{};
    std::string dll_build;
    u32 protocol = 0;
    GameBuild game;
    std::array<u8, 32> exe_sha256{};
    u32 pid = 0;
};
REBOOT_CONTRACT_FRAME(GcHello, 0x400)

struct DllFeatures {
    bool auth_redirect = false;
    bool console = false;
    bool memory_fix = false;
    bool exit_suppression = false;
};

// `origin` is the front URL with its /s/<session_key>/ prefix.
struct ClientDllConfig {
    Uuid session_id;
    std::string origin;
    std::vector<std::string> sentinels;
    std::vector<std::string> host_suffixes;
    bool ws_rewrite = false;
    DllFeatures features;
    std::string console_key;
    bool test_mode = false;
    LogLevel log_level{};
};

struct GcWelcome {
    ClientDllConfig config;
};
REBOOT_CONTRACT_FRAME(GcWelcome, 0x401)

// DLL -> engine events

struct Loaded {
    std::string dll_version;
    u32 table_version = 0;
    BuildMatch build_match{};
};
REBOOT_CONTRACT_FRAME(Loaded, 0x410)

struct PatchResult {
    std::string id;
    PatchStatus status{};
    u64 rva = 0;
};
REBOOT_CONTRACT_FRAME(PatchResult, 0x411)

struct RedirectReady {};
REBOOT_CONTRACT_FRAME(RedirectReady, 0x412)

struct HookFailed {
    std::string step;
    bool required = false;
};
REBOOT_CONTRACT_FRAME(HookFailed, 0x413)

struct LoggedIn {};
REBOOT_CONTRACT_FRAME(LoggedIn, 0x414)

struct WindowCreated {
    u64 hwnd = 0;
};
REBOOT_CONTRACT_FRAME(WindowCreated, 0x415)

struct ExitRequested {
    ExitKind kind{};
    i32 code = 0;
    std::string phase;
};
REBOOT_CONTRACT_FRAME(ExitRequested, 0x416)

struct ConsoleReady {};
REBOOT_CONTRACT_FRAME(ConsoleReady, 0x417)

struct Fatal {
    std::string step;
};
REBOOT_CONTRACT_FRAME(Fatal, 0x418)

// Map travel began; the session reports no hang until TravelEnded.
struct TravelStarted {};
REBOOT_CONTRACT_FRAME(TravelStarted, 0x419)

struct TravelEnded {};
REBOOT_CONTRACT_FRAME(TravelEnded, 0x41A)

// The game entered the match at `address`, as TestJoin asked.
struct Joined {
    std::string address;
};
REBOOT_CONTRACT_FRAME(Joined, 0x41B)

// The match connection dropped; `reason` is the game's own network failure text.
struct Disconnected {
    std::string reason;
};
REBOOT_CONTRACT_FRAME(Disconnected, 0x41C)

// engine -> DLL requests; TestJoin and TestQuit only when test_mode is set.

struct GcShutdown {
    u64 req_id = 0;
};
REBOOT_CONTRACT_FRAME(GcShutdown, 0x420)

struct TestJoin {
    u64 req_id = 0;
    std::string address;
};
REBOOT_CONTRACT_FRAME(TestJoin, 0x421)

struct TestQuit {
    u64 req_id = 0;
};
REBOOT_CONTRACT_FRAME(TestQuit, 0x422)

}  // namespace reboot::contracts::game_client
