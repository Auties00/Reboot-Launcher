#pragma once

#include <algorithm>
#include <string_view>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::ipc {

using Compatibility = contracts::ipc::Compatibility;

// Engine IPC has no capability negotiation: only the same build gets Full.
[[nodiscard]] constexpr Compatibility compatibility_for(std::string_view engine_build,
                                                        std::string_view client_build) noexcept {
    return engine_build == client_build ? Compatibility::Full : Compatibility::BootstrapOnly;
}

[[nodiscard]] constexpr bool is_bootstrap_method(u32 method_id) noexcept {
    return std::ranges::find(contracts::ipc::kBootstrapMethodIds, method_id) !=
           contracts::ipc::kBootstrapMethodIds.end();
}

// A refused method fails with ipc.version_mismatch.
[[nodiscard]] constexpr bool allows_method(Compatibility compatibility, u32 method_id) noexcept {
    return compatibility == Compatibility::Full || is_bootstrap_method(method_id);
}

}  // namespace rb::ipc
