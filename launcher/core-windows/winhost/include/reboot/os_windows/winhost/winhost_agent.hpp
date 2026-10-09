#pragma once

#include <cstddef>
#include <memory>

#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/os_windows/winhost/winhost_bootstrap.hpp"
#include "reboot/os_windows/winhost/winhost_exit.hpp"
#include "reboot/os_windows/winhost/winhost_failure.hpp"

namespace reboot::os_windows::winhost {

class ControlConnection;

// Largest Output payload per frame; a pipe read is split into chunks of this size.
inline constexpr std::size_t kOutputChunkBytes = std::size_t{64} << 10;
static_assert(kOutputChunkBytes < kGameControlFrameCap);

// ERROR_INVALID_IMAGE_HASH: Injected.error when a DLL's sha256 does not match its InjectSpec.
inline constexpr i64 kInjectHashMismatch = 577;

// Covers no capability ids (decisions process-model, linux-compat-layer, macos-compat-layer,
// game-control-channel, dll-toolchain-av, release-pipeline).
// The play session inside the Wine prefix, driven by the engine over one ControlConnection.
// - hello() sends WhHello{token, REBOOT_WINHOST_BUILD, kWinhostProtocol, pid}; the build is
//   winhost's own payload release, not the launcher's ipc_build.
// - WhWelcome{SpawnGame}: Win32Session parks the park_utf16 files until the session ends, then
//   creates the game CREATE_SUSPENDED with the given UTF-16 argv and cwd, and the given
//   environment block laid over winhost's own environment (without REBOOT_CTL*), and the
//   companions with winhost's own environment and the game's cwd, all in one KILL_ON_JOB_CLOSE Job. It then injects the Early entries: EarlyBirdApc before resume,
//   AfterResume once Resume arrives. LoggedIn entries wait for an Inject request. Spawned{Game|Companion} follows each creation, Injected each entry.
//   A failed launch sends WhFatal, terminates the Job and ends run() with LaunchFailed.
// - Every InjectSpec is opened deny-write, hashed through that handle and compared with its
//   sha256 before the load, under Wine as on Windows; the handle is held until the session ends.
//   A mismatch is Injected{ok = false, error = kInjectHashMismatch} and nothing is loaded.
// - The argv and environment bytes are wiped once CreateProcessW has them. Wine may still copy
//   the Windows command line and environment into the game's Unix argv and envp, so the
//   macOS/Linux spike must check /proc/<game pid>/cmdline and environ for -AUTH_PASSWORD and
//   REBOOT_CTL_TOKEN.
// - The run() thread reads frames and answers Ping with Pong itself. Resume, Inject and Stop run
//   in arrival order on one request thread, so a waited remote load or a Stop grace never delays
//   a Pong. Each gets a CommandResult (failed_reply on failure); any other type gets Unsupported.
// - Resume resumes the game's main thread (companions are never resumed); Inject{entry} injects
//   one entry. Stop{grace_ms} is answered ok at once, gives the Job grace_ms to empty, then
//   terminates it; once it is empty and its Exited events are sent, winhost disconnects and run()
//   returns Stopped. This is the sequence FakeWinhost plays. A Job that does not empty within
//   drain_timeout_ms is reported as WhFatal{stop} before the disconnect.
// - Output{role, stream, bytes} relays each process's stdout and stderr pipe in chunks of at
//   most kOutputChunkBytes; Exited{role, code} follows each process exit. Exited has no pid, so
//   two companions' exits cannot be told apart. A game that exits on its own leaves winhost
//   serving until Stop or EOF.
// - EOF, or a socket error, calls TerminateJobObject, waits until the Job has no active
//   process and returns EngineClosed, so an engine crash ends the game.
class WinhostAgent {
public:
    explicit WinhostAgent(ControlConnection& connection);
    // Terminates a live Job and waits for it to empty.
    ~WinhostAgent();
    WinhostAgent(const WinhostAgent&) = delete;
    WinhostAgent& operator=(const WinhostAgent&) = delete;

    // FailureStep::Handshake when the send fails.
    Expected<void> hello(const ControlTokenBytes& token);

    // Serves the channel until it ends: Stopped, EngineClosed after a Welcome, Refused on EOF
    // before one, LaunchFailed, or ProtocolError for a malformed, oversized or out-of-order frame,
    // which is reported as WhFatal{protocol}. Every way out ends the Job first.
    [[nodiscard]] WinhostExit run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The whole program: read_bootstrap(), ControlConnection::connect(), hello() and run(). Errors
// before the channel exists go to stderr, which the runner's log captures, without the token.
// Every exception is caught and becomes InternalError.
[[nodiscard]] WinhostExit run_winhost() noexcept;

}  // namespace reboot::os_windows::winhost
