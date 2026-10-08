#pragma once

#include <chrono>
#include <expected>
#include <memory>
#include <variant>

#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/os_windows/win32session/inject_error.hpp"
#include "reboot/os_windows/win32session/spawn_error.hpp"

namespace reboot::os_windows::win32session {

// Emitted on the pipe-reader and exit-wait threads, so the callback must be thread-safe. These are
// the winhost wire structs so the engine (native Windows) and reboot-winhost.exe (under Wine) emit
// one vocabulary.
using SessionEvent = std::variant<contracts::winhost::Spawned, contracts::winhost::Injected,
                                  contracts::winhost::Output, contracts::winhost::Exited>;

using EventSink = UniqueFunction<void(SessionEvent)>;

// A launched session tree: the game and its companions created suspended in one
// KILL_ON_JOB_CLOSE Job, the deny-write handles that pin each injected DLL for the session's
// lifetime, the parked files, and the pipe-reader and exit-wait threads. Destruction terminates
// the Job and waits until it is empty, so an abandoned session leaves nothing running, then
// restores the parked files.
class Win32Session {
public:
    virtual ~Win32Session() = default;

    // A LoggedIn-phase DLL: hashed through its own deny-write handle, loaded with a waited,
    // exit-code-checked remote LoadLibraryW, and the handle held until the session ends.
    virtual std::expected<void, InjectError> inject(const contracts::winhost::InjectSpec& spec) = 0;

    // Resumes the game's main thread; companions are never resumed. Early APC entries load as the
    // thread starts, and their remote path buffers are reclaimed once confirmed.
    virtual std::expected<void, SpawnError> resume() = 0;

    // Gives the Job `grace` to drain after an agent shutdown, then terminates it and waits until it
    // has no active process.
    virtual void stop(std::chrono::milliseconds grace) = 0;
};

// Parks each SpawnGame::park_utf16 file as <name>.reboot-parked, replacing a leftover from a
// crashed session; a missing file or a failed rename is skipped. Then creates the game and
// companions suspended in one Job, applies the Early injections (EarlyBirdApc queues LoadLibraryW
// on the game's main thread; AfterResume is deferred to resume()), and returns before resuming. Spawned and Injected events are delivered through `on_event` as they happen. A
// failure terminates the Job and waits for it to empty before returning the error.
// Covers game-launch.process-creation, dll-injection.mechanism, dll-injection.+7, dll-injection.+40.
[[nodiscard]] std::expected<std::unique_ptr<Win32Session>, SpawnError> launch_session(
    const contracts::winhost::SpawnGame& spawn, EventSink on_event);

}  // namespace reboot::os_windows::win32session
