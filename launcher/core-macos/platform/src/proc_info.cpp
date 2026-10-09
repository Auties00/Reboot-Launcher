#include "darwin.hpp"

#include "proc_info.hpp"

#include <libproc.h>
#include <signal.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <array>
#include <cerrno>
#include <cstring>

#include "reboot/posix/posix_error.hpp"

namespace rb::os_macos::platform {

namespace {

// False with errno set on failure; true with `found` false when the pid names no process.
[[nodiscard]] bool read_kinfo(u32 pid, kinfo_proc& info, bool& found) {
    std::array<int, 4> mib{CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>(pid)};
    std::size_t size = sizeof info;
    if (::sysctl(mib.data(), static_cast<u_int>(mib.size()), &info, &size, nullptr, 0) != 0) return false;
    found = size >= sizeof info && info.kp_proc.p_pid == static_cast<pid_t>(pid);
    return true;
}

}  // namespace

Result<std::optional<std::chrono::system_clock::time_point>> process_start_time(u32 pid) {
    kinfo_proc info{};
    bool found = false;
    if (!read_kinfo(pid, info, found)) {
        if (errno == ESRCH) return std::optional<std::chrono::system_clock::time_point>{};
        return std::unexpected(posix::call_failed("sysctl", errno));
    }
    if (!found) return std::optional<std::chrono::system_clock::time_point>{};
    const timeval started = info.kp_proc.p_starttime;
    const auto since_epoch = std::chrono::seconds{started.tv_sec} + std::chrono::microseconds{started.tv_usec};
    return std::optional<std::chrono::system_clock::time_point>{std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(since_epoch)}};
}

std::optional<std::string> process_command_name(u32 pid) {
    kinfo_proc info{};
    bool found = false;
    if (!read_kinfo(pid, info, found) || !found) return std::nullopt;
    const char* name = info.kp_proc.p_comm;
    return std::string(name, ::strnlen(name, sizeof info.kp_proc.p_comm));
}

bool has_exited(u32 pid) noexcept {
    siginfo_t info{};
    while (::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT) != 0) {
        if (errno != EINTR) return errno == ECHILD;
    }
    return info.si_pid == static_cast<pid_t>(pid);
}

Result<std::vector<u32>> child_pids(u32 parent) {
    std::vector<pid_t> pids(256);
    for (;;) {
        const int capacity = static_cast<int>(pids.size() * sizeof(pid_t));
        const int bytes = ::proc_listpids(PROC_PPID_ONLY, parent, pids.data(), capacity);
        if (bytes < 0) return std::unexpected(posix::call_failed("proc_listpids", errno));
        // A full buffer may have cut the list short.
        if (bytes < capacity) {
            std::vector<u32> out;
            const std::size_t count = static_cast<std::size_t>(bytes) / sizeof(pid_t);
            for (std::size_t i = 0; i < count; ++i)
                if (pids[i] > 0) out.push_back(static_cast<u32>(pids[i]));
            return out;
        }
        pids.resize(pids.size() * 2);
    }
}

}  // namespace rb::os_macos::platform
