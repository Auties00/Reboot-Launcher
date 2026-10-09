#include "child_reaping.hpp"

#include <cerrno>
#include <signal.h>
#include <sys/wait.h>
#include <unordered_map>

namespace rb::os_linux::platform {

namespace {

struct Table {
    std::mutex mutex;
    // A recorded status, or nullopt while the child has not been reaped by the orphan reaper.
    std::unordered_map<u32, std::optional<ReapedStatus>> children;
};

[[nodiscard]] Table& table() {
    static Table instance;
    return instance;
}

}  // namespace

std::unique_lock<std::mutex> ChildTable::lock() { return std::unique_lock{table().mutex}; }

void ChildTable::add(u32 pid) { table().children[pid] = std::nullopt; }

bool ChildTable::contains(u32 pid) { return table().children.contains(pid); }

void ChildTable::record(u32 pid, ReapedStatus status) {
    const auto found = table().children.find(pid);
    if (found != table().children.end()) found->second = status;
}

std::optional<ReapedStatus> ChildTable::remove(u32 pid) {
    const auto found = table().children.find(pid);
    if (found == table().children.end()) return std::nullopt;
    const std::optional<ReapedStatus> status = found->second;
    table().children.erase(found);
    return status;
}

WaitResult reap_listed_child(u32 pid) {
    const auto held = ChildTable::lock();
    for (;;) {
        siginfo_t info{};
        if (::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG) == 0) {
            if (info.si_pid == 0) return {};
            (void)ChildTable::remove(pid);
            return {WaitOutcome::Ended, {info.si_code, info.si_status}};
        }
        if (errno == EINTR) continue;
        if (const std::optional<ReapedStatus> recorded = ChildTable::remove(pid))
            return {WaitOutcome::Ended, *recorded};
        return {WaitOutcome::Lost, {}};
    }
}

PeekResult peek_listed_child(u32 pid) {
    const auto held = ChildTable::lock();
    for (;;) {
        siginfo_t info{};
        if (::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT) == 0) {
            if (info.si_pid == 0) return {};
            return {{WaitOutcome::Ended, {info.si_code, info.si_status}}, true};
        }
        if (errno == EINTR) continue;
        if (const std::optional<ReapedStatus> recorded = ChildTable::remove(pid))
            return {{WaitOutcome::Ended, *recorded}, false};
        return {{WaitOutcome::Lost, {}}, false};
    }
}

void reap_zombies() {
    // Bounded, so a zombie that cannot be reaped never spins the caller.
    for (int round = 0; round < 4096; ++round) {
        const auto held = ChildTable::lock();
        siginfo_t peeked{};
        if (::waitid(P_ALL, 0, &peeked, WEXITED | WNOHANG | WNOWAIT) != 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (peeked.si_pid == 0) return;
        siginfo_t reaped{};
        if (::waitid(P_PID, static_cast<id_t>(peeked.si_pid), &reaped, WEXITED | WNOHANG) != 0 || reaped.si_pid == 0)
            continue;
        ChildTable::record(static_cast<u32>(reaped.si_pid), {reaped.si_code, reaped.si_status});
    }
}

}  // namespace rb::os_linux::platform
