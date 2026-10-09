#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::os_linux::ipc {

// What LinuxCallerContext::detect reads from the process.
struct CallerFacts {
    // "NAME=value" entries in environment order.
    std::span<const std::string_view> environment;
    // /proc/self/sessionid and /proc/self/loginuid; nullopt when unreadable.
    std::optional<std::string_view> session_id;
    std::optional<std::string_view> login_uid;
    u32 euid = 0;
    u32 uid = 0;
};

// The rules LinuxCallerContext documents, over facts already read.
[[nodiscard]] ports::CallerContext caller_context_from(const CallerFacts& facts);

}  // namespace rb::os_linux::ipc
