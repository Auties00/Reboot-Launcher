#pragma once

#include <optional>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace reboot::os_linux::ipc {

// The first fd systemd passes; sd_listen_fds(3) numbers them from here.
inline constexpr int kListenFdsStart = 3;

// LISTEN_PID and LISTEN_FDS as the service manager set them.
struct ListenEnvironment {
    std::optional<std::string_view> listen_pid;
    std::optional<std::string_view> listen_fds;
};

// What UnixSocketListener does with the LISTEN_* variables of process `pid`.
struct SocketActivation {
    // LISTEN_PID names `pid`; otherwise the variables belong to an ancestor and are ignored.
    bool for_us = false;
    // LISTEN_FDS, when it is a plain decimal count.
    std::optional<u32> count;
};

[[nodiscard]] SocketActivation read_socket_activation(const ListenEnvironment& environment, u32 pid) noexcept;

}  // namespace reboot::os_linux::ipc
