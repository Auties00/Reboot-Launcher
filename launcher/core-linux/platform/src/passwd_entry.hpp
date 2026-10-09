#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::os_linux::platform {

struct PasswdEntry {
    std::string name;
    NativePath home;
};

// getpwuid_r; nullopt when `uid` has no entry or it names no absolute home.
[[nodiscard]] std::optional<PasswdEntry> read_passwd(u32 uid);

}  // namespace rb::os_linux::platform
