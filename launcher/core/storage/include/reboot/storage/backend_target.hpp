#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/enum_names.hpp"

namespace rb::storage {

inline constexpr Port kDefaultBackendPort{3551};
// The port the game dials for XMPP, used when an XMPP endpoint names none.
inline constexpr Port kDefaultXmppPort{80};

// Embedded runs our reboot-backend; the engine front proxies to a Local or Remote one.
enum class BackendKind : u8 { Embedded, Local, Remote };
enum class BackendScheme : u8 { Http, Https };

template <>
struct EnumNames<BackendKind> {
    static constexpr std::array<std::string_view, 3> kNames{"embedded", "local", "remote"};
};
template <>
struct EnumNames<BackendScheme> {
    static constexpr std::array<std::string_view, 2> kNames{"http", "https"};
};

// Always plain http.
struct LocalBackendAddress {
    HostPort endpoint{"127.0.0.1", kDefaultBackendPort};
    // Where the auth DLL redirects the game's XMPP connect; unset leaves it alone.
    std::optional<HostPort> xmpp;

    bool operator==(const LocalBackendAddress&) const = default;
};

struct RemoteBackendAddress {
    // Unset tries https, then http.
    std::optional<BackendScheme> scheme;
    HostPort endpoint;
    std::optional<HostPort> xmpp;

    bool operator==(const RemoteBackendAddress&) const = default;
};

// Each kind keeps its own address, so switching kinds never loses what the user typed.
struct BackendTarget {
    BackendKind kind = BackendKind::Embedded;
    LocalBackendAddress local;
    std::optional<RemoteBackendAddress> remote;

    bool operator==(const BackendTarget&) const = default;

    // Lowercases hosts, fills absent ports; storage.invalid_backend_target or storage.invalid_host.
    [[nodiscard]] static Result<BackendTarget> normalize(BackendTarget target);
};

}  // namespace rb::storage
