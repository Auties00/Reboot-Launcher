#pragma once

#include <optional>

#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace reboot::catalog {

// Mirrors contracts::winhost::BootStrategy; injection maps one to the other.
enum class BootStrategy : u8 { EarlyBirdApc, AfterResume };

// One value per runner, since the early-bird spike passes or fails per runner. Absent: that
// runner's default applies.
struct BootInject {
    std::optional<BootStrategy> native;
    std::optional<BootStrategy> wine;
    std::optional<BootStrategy> umu;
    std::optional<BootStrategy> mac_runtime;

    bool operator==(const BootInject&) const = default;
};

// Whether the backend serves CloudStorage hotfixes to the build (cloudstorage-hotfix-content).
enum class HotfixDelivery : u8 { Serve, Withhold };

enum class XmppSupport : u8 { Unknown, Supported, Unsupported };
enum class XmppTransport : u8 { Unknown, WebSocket, RawTcp };

// Spike findings per range (backend-port-80); only feeds XmppUnavailable diagnostics.
struct XmppNote {
    XmppSupport support = XmppSupport::Unknown;
    XmppTransport transport = XmppTransport::Unknown;

    bool operator==(const XmppNote&) const = default;
};

struct BuildFlags {
    BootInject boot_inject;
    // -AUTH_TYPE=exchangecode; otherwise epic with a single-use launch secret.
    bool auth_exchangecode = false;
    XmppNote xmpp;
    // Withhold unless the catalog marks a range verified, as cloudstorage-hotfix-content requires.
    HotfixDelivery hotfix_delivery = HotfixDelivery::Withhold;
    // Read only by process EnvBuilder, the one place OPENSSL_ia32cap is set.
    bool openssl_ia32cap = false;

    bool operator==(const BuildFlags&) const = default;
};

// Inclusive on both ends.
struct VersionRange {
    GameVersion first;
    GameVersion last;

    [[nodiscard]] constexpr bool contains(const GameVersion& version) const noexcept {
        return first <= version && version <= last;
    }

    bool operator==(const VersionRange&) const = default;
};

struct BuildFlagRange {
    VersionRange versions;
    BuildFlags flags;

    bool operator==(const BuildFlagRange&) const = default;
};

}  // namespace reboot::catalog
