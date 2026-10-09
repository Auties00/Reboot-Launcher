#pragma once

#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::ipc {

// Exactly 16 lowercase hex digits, as root_hash16() produces.
[[nodiscard]] bool is_root_hash16(std::string_view text) noexcept;

// ports::endpoint_name after checking both inputs; bad input is ipc.invalid_endpoint_input.
[[nodiscard]] Result<std::string> endpoint_for(const ports::PeerIdentity& user, std::string_view root_hash16);

}  // namespace rb::ipc
