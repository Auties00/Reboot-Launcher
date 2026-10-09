#pragma once

#include <chrono>
#include <expected>
#include <memory>
#include <variant>
#include <vector>

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

// The Job still held these processes when SpawnGame::drain_timeout_ms ran out after it was
// terminated; `pids` is empty when the Job's list could not be read. Their Exited events never come.
struct StuckProcesses {
    std::vector<u32> pids;
};

// A launched session tree: the game and its companions created suspended in one
// KILL_ON_JOB_CLOSE Job, the deny-write handles that pin each injected DLL for the session's
// lifetime, the parked files, and the pipe-reader and exit-wait threads. Destruction terminates
// the Job, waits at most drain_timeout_ms for it to empty, stops the threads, then restores the
// parked files. A thread that cannot be stopped within drain_timeout_ms is left running with the
// sink cleared, so no event reaches the caller once stop() or destruction returns. Not
// thread-safe: inject, resume and stop are called from one thread at a time.
class Win32Session {
public:
    virtual ~Win32Session() = default;

    // A LoggedIn-phase DLL: hashed through its own deny-write handle, loaded with a remote
    // LoadLibraryW waited for at most inject_timeout_ms and exit-code-checked, and the handle held
    // until the session ends, also after a timed-out load. Injected follows either way.
    virtual std::expected<void, InjectError> inject(const contracts::winhost::InjectSpec& spec) = 0;

    // Resumes the game's main thread; companions are never resumed. A failed resume terminates the
    // Job and returns SpawnStep::Resume. Each EarlyBirdApc entry must
    // then show up in the module list within inject_timeout_ms, and its remote path buffer is
    // reclaimed once it does; AfterResume entries load now. Injected follows each entry. An Early
    // entry that fails terminates the Job, since the game must not run without it, and returns
    // SpawnStep::Inject.
    virtual std::expected<void, SpawnError> resume() = 0;

    // Gives the game `grace` to exit after an agent shutdown (the never-resumed companions do not
    // count), then terminates the Job and waits at most drain_timeout_ms for it to empty. A Job
    // that does not empty is reported. The readers then get drain_timeout_ms to reach EOF and as
    // long again once their reads are cancelled, so stop() returns within grace + 3 x drain.
    virtual std::expected<void, StuckProcesses> stop(std::chrono::milliseconds grace) = 0;
};

// Parks each SpawnGame::park_utf16 file as <name>.reboot-parked, replacing a leftover from a
// crashed session; a missing file or a failed rename is skipped. Then creates the game and
// companions suspended in one Job (the game with the given environment block, or the caller's
// environment when it is empty; companions always with the caller's), applies the Early
// injections (EarlyBirdApc queues LoadLibraryW on the game's main thread; AfterResume is deferred
// to resume()), and returns before resuming. Spawned and Injected events are delivered through
// `on_event` as they happen. A failure terminates the Job and waits for it to empty before
// returning the error. The command line and environment copies are wiped once the game has them.
// Covers game-launch.process-creation, dll-injection.mechanism, dll-injection.+7, dll-injection.+40.
[[nodiscard]] std::expected<std::unique_ptr<Win32Session>, SpawnError> launch_session(
    const contracts::winhost::SpawnGame& spawn, EventSink on_event);

}  // namespace reboot::os_windows::win32session
