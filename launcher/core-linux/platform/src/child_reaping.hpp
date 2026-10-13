#pragma once

#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

#include "reboot/foundation/types.hpp"

namespace rb::os_linux::platform {

// How a child ended, as waitid reports it: si_code is CLD_EXITED, CLD_KILLED or CLD_DUMPED.
struct ReapedStatus {
    int code = 0;
    int status = 0;
};

// As PID 1 the engine also reaps re-parented orphans. Children that the package waits for itself
// are listed here, so the orphan reaper records their status instead of discarding it. The lock
// is held from before a spawn until its pid is listed, so no child is reaped unlisted.
class ChildTable {
public:
    [[nodiscard]] static std::unique_lock<std::mutex> lock();

    // All of these require lock().
    static void add(u32 pid);
    [[nodiscard]] static bool contains(u32 pid);
    // Keeps the status for the waiter of a listed pid; does nothing for an unlisted one.
    static void record(u32 pid, ReapedStatus status);
    // Removes `pid` and returns a status the orphan reaper recorded for it.
    [[nodiscard]] static std::optional<ReapedStatus> remove(u32 pid);
};

enum class WaitOutcome : u8 { Running, Ended, Lost };

struct WaitResult {
    WaitOutcome outcome = WaitOutcome::Running;
    ReapedStatus status;
};

// Reaps the listed child `pid` without blocking and unlists it once it ended. Lost means its
// status is gone: reaped elsewhere, as when SIGCHLD is ignored.
[[nodiscard]] WaitResult reap_listed_child(u32 pid);

struct PeekResult {
    WaitResult waited;
    // Ended and still a zombie, so neither its pid nor the group id it led can be reused until
    // reap_listed_child; false when the orphan reaper or someone else already reaped it.
    bool zombie = false;
};

// reap_listed_child without reaping: an ended child stays listed and a zombie.
[[nodiscard]] PeekResult peek_listed_child(u32 pid);

// For PID 1: reaps every unlisted zombie child, such as a re-parented orphan. Listed children are
// left to their waiters, so a leader stays a zombie and its group id cannot be reused before
// terminate_tree's group kill. The children come from /proc/self/task/*/children; without that
// file (CONFIG_PROC_CHILDREN off) every zombie is reaped, listed ones recorded in ChildTable.
void reap_zombies();

// The pids of a /proc/<pid>/task/<tid>/children file: decimal, space separated.
[[nodiscard]] std::vector<u32> parse_child_pids(std::string_view text);

}  // namespace rb::os_linux::platform
