#pragma once

#include <string_view>

#include "reboot/backend/backend_target.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/storage/settings_values.hpp"

namespace rb::backend {

inline constexpr std::string_view kLoopbackBindAddress = "127.0.0.1";
inline constexpr std::string_view kLanBindAddress = "0.0.0.0";

// What BackendService::reconfigure compares: every field is one a running backend must restart
// to apply. The console key is per session, and the log level is BackendProcess's.
struct BackendConfig {
    BackendTarget target;
    // Embedded only; off means loopback, so no other machine reaches the backend.
    bool allow_lan = false;

    bool operator==(const BackendConfig&) const = default;

    [[nodiscard]] std::string_view bind_address() const noexcept {
        return allow_lan ? kLanBindAddress : kLoopbackBindAddress;
    }

    [[nodiscard]] static Result<BackendConfig> from_settings(const storage::BackendSettings& settings);
};

}  // namespace rb::backend
