#pragma once

#include <cstddef>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace rb::secrets {

enum class SecretKind : u8 {
    // Password of the user's account on a remote backend; scope is the backend host.
    RemoteBackendPassword,
    // Password players need to join a server the user hosts; scope is the host profile.
    HostJoinPassword,
    // Password typed to join someone else's server; scope is its NeedsJoinPassword request.
    JoinPassword,
};

// Session secrets live in engine memory until the engine exits; Remember also writes them
// to the secret store.
enum class Retention : u8 { Session, Remember };

inline constexpr std::size_t kMaxSecretBytes = 1024;

[[nodiscard]] constexpr std::string_view kind_name(SecretKind kind) noexcept {
    switch (kind) {
        case SecretKind::RemoteBackendPassword: return "remote-backend-password";
        case SecretKind::HostJoinPassword: return "host-join-password";
        case SecretKind::JoinPassword: return "join-password";
    }
    return "";
}

// What a put that names no retention gets; a remote password is stored only on opt-in.
[[nodiscard]] constexpr Retention default_retention(SecretKind kind) noexcept {
    return kind == SecretKind::HostJoinPassword ? Retention::Remember : Retention::Session;
}

// Only a remote password lets the caller choose.
[[nodiscard]] constexpr bool retention_allowed(SecretKind kind, Retention retention) noexcept {
    return kind == SecretKind::RemoteBackendPassword || retention == default_retention(kind);
}

// Hosts hand the join password to their players, so it is the one secret a UI may read back.
[[nodiscard]] constexpr bool revealable(SecretKind kind) noexcept { return kind == SecretKind::HostJoinPassword; }

}  // namespace rb::secrets
