#pragma once

#include <optional>
#include <variant>

#include "reboot/backend/backend_url.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/storage/backend_target.hpp"

namespace reboot::backend {

// Our reboot-backend, spawned and supervised by the engine.
struct EmbeddedBackend {
    bool operator==(const EmbeddedBackend&) const = default;
};

// Another backend on this PC; always plain http.
struct LocalBackend {
    HostPort endpoint{"127.0.0.1", kDefaultBackendPort};
    // Where the auth DLL sends the game's XMPP connect; unset leaves it alone, so a backend
    // that listens on :80 itself keeps working.
    std::optional<HostPort> xmpp;

    bool operator==(const LocalBackend&) const = default;
};

struct RemoteBackend {
    BackendUrl url;
    // As LocalBackend::xmpp; a Reboot upstream's backend-info ws_port is used when unset.
    std::optional<HostPort> xmpp;

    bool operator==(const RemoteBackend&) const = default;
};

// The selected kind only: storage remembers every kind's address, and apply_to() keeps the others.
// Local and Remote spawn nothing: the engine front proxies sessions to them.
struct BackendTarget {
    std::variant<EmbeddedBackend, LocalBackend, RemoteBackend> value;

    bool operator==(const BackendTarget&) const = default;

    [[nodiscard]] storage::BackendKind kind() const noexcept;
    [[nodiscard]] bool embedded() const noexcept { return std::holds_alternative<EmbeddedBackend>(value); }

    // The upstream the front and RemoteBackendProbe use; nullopt for Embedded.
    [[nodiscard]] std::optional<BackendUrl> upstream_url() const;
    // The XMPP endpoint the user entered for Local or Remote.
    [[nodiscard]] std::optional<HostPort> xmpp() const;

    // The stored kind with its address, scheme included; a Remote kind with no remote address
    // fails with backend.remote_address_missing.
    [[nodiscard]] static Result<BackendTarget> from_settings(const storage::BackendTarget& stored);
    // Selects this kind and replaces its address; the other kinds' addresses are kept.
    void apply_to(storage::BackendTarget& stored) const;
};

}  // namespace reboot::backend
