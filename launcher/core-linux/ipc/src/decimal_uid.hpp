#pragma once

#include <optional>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace rb::os_linux::ipc {

// PeerIdentity::user_id back to a uid: decimal digits as std::to_string writes them (no sign, no
// leading zero), within u32 and not 4294967295, which is (uid_t)-1.
[[nodiscard]] std::optional<u32> parse_decimal_uid(std::string_view text) noexcept;

}  // namespace rb::os_linux::ipc
