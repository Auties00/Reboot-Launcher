#pragma once

#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

// glibc 2.28 (the EL8 floor) wraps none of these, and its kernel headers predate their numbers,
// which are the same on every architecture we build.
namespace reboot::os_linux::platform {

#ifdef SYS_pidfd_send_signal
inline constexpr long kSysPidfdSendSignal = SYS_pidfd_send_signal;
#else
inline constexpr long kSysPidfdSendSignal = 424;
#endif
#ifdef SYS_pidfd_open
inline constexpr long kSysPidfdOpen = SYS_pidfd_open;
#else
inline constexpr long kSysPidfdOpen = 434;
#endif
#ifdef SYS_clone3
inline constexpr long kSysClone3 = SYS_clone3;
#else
inline constexpr long kSysClone3 = 435;
#endif
#ifdef SYS_close_range
inline constexpr long kSysCloseRange = SYS_close_range;
#else
inline constexpr long kSysCloseRange = 436;
#endif

// -1 with errno set on failure.
[[nodiscard]] inline int sys_pidfd_open(pid_t pid) noexcept {
    return static_cast<int>(::syscall(kSysPidfdOpen, pid, 0));
}

[[nodiscard]] inline int sys_pidfd_send_signal(int pidfd, int signal) noexcept {
    return static_cast<int>(::syscall(kSysPidfdSendSignal, pidfd, signal, nullptr, 0));
}

}  // namespace reboot::os_linux::platform
