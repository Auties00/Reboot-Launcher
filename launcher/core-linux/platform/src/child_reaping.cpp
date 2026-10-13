#include "child_reaping.hpp"

#include <cerrno>
#include <charconv>
#include <filesystem>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <system_error>
#include <unordered_map>

#include "text_files.hpp"

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

// Every child of every thread of this process; nullopt when the kernel has no children files.
[[nodiscard]] std::optional<std::vector<u32>> own_children() {
    std::error_code error;
    std::filesystem::directory_iterator tasks{"/proc/self/task", error};
    if (error) return std::nullopt;
    std::vector<u32> children;
    bool listed = false;
    // increment(error), since operator++ throws on the I/O loop; an error ends the listing early.
    for (const std::filesystem::directory_iterator end; tasks != end; tasks.increment(error)) {
        const std::optional<std::string> text = try_read_text_file(tasks->path() / "children");
        if (!text) continue;
        listed = true;
        for (const u32 pid : parse_child_pids(*text)) children.push_back(pid);
    }
    if (!listed) return std::nullopt;
    return children;
}

// The fallback: one zombie a round, listed or not.
void reap_any_zombies() {
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
    // Under the lock, so a child spawned meanwhile is listed before it is looked at.
    auto held = ChildTable::lock();
    const std::optional<std::vector<u32>> children = own_children();
    if (!children) {
        held.unlock();
        return reap_any_zombies();
    }
    for (const u32 pid : *children) {
        if (ChildTable::contains(pid)) continue;
        // Not a zombie yet: nothing happens. Gone already: ECHILD.
        for (;;) {
            siginfo_t info{};
            if (::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG) == 0 || errno != EINTR) break;
        }
    }
}

std::vector<u32> parse_child_pids(std::string_view text) {
    std::vector<u32> pids;
    while (!text.empty()) {
        const std::size_t start = text.find_first_not_of(" \n");
        if (start == std::string_view::npos) break;
        text.remove_prefix(start);
        u32 pid = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), pid);
        if (error != std::errc{} || pid == 0) break;
        pids.push_back(pid);
        text.remove_prefix(static_cast<std::size_t>(end - text.data()));
    }
    return pids;
}

}  // namespace rb::os_linux::platform
