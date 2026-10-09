#pragma once

#include <vector>

#include "reboot/foundation/net_types.hpp"
#include "reboot/ports/runner.hpp"

namespace rb::injection {

// Compiled into the custom auth DLLs that NetMode::LegacyFixed exists for.
inline constexpr Port kLegacyBackendPort{3551};
inline constexpr Port kLegacyXmppPort{80};

// What a NetMode::LegacyFixed session binds.
struct LegacyRequirements {
    // On 127.0.0.1, unprefixed; a busy port is reported, never freed by killing its owner.
    std::vector<Endpoint> fixed_listeners;

    // False without :80; the session then raises XmppUnavailable{ThirdPartyAuthDll}.
    [[nodiscard]] bool xmpp_available() const noexcept;

    bool operator==(const LegacyRequirements&) const = default;
};

// Native (Windows): 127.0.0.1:3551 and :80. Wine runners (macOS, Linux): :3551 only, even where
// the OS allows an unprivileged :80 bind, as macOS 10.14 and later do.
[[nodiscard]] LegacyRequirements legacy_requirements(ports::RunnerKind runner);

}  // namespace rb::injection
