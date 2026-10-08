#include "reboot/backend/backend_target.hpp"

#include <utility>

#include "messages.hpp"

namespace reboot::backend {

namespace {

struct KindOf {
    storage::BackendKind operator()(const EmbeddedBackend&) const noexcept { return storage::BackendKind::Embedded; }
    storage::BackendKind operator()(const LocalBackend&) const noexcept { return storage::BackendKind::Local; }
    storage::BackendKind operator()(const RemoteBackend&) const noexcept { return storage::BackendKind::Remote; }
};

[[nodiscard]] std::optional<net::UrlScheme> to_url_scheme(std::optional<storage::BackendScheme> scheme) noexcept {
    if (!scheme) return std::nullopt;
    return *scheme == storage::BackendScheme::Http ? net::UrlScheme::Http : net::UrlScheme::Https;
}

[[nodiscard]] std::optional<storage::BackendScheme> to_stored_scheme(std::optional<net::UrlScheme> scheme) noexcept {
    if (!scheme) return std::nullopt;
    return *scheme == net::UrlScheme::Http ? storage::BackendScheme::Http : storage::BackendScheme::Https;
}

}  // namespace

storage::BackendKind BackendTarget::kind() const noexcept { return std::visit(KindOf{}, value); }

std::optional<BackendUrl> BackendTarget::upstream_url() const {
    if (const auto* local = std::get_if<LocalBackend>(&value))
        return BackendUrl{net::UrlScheme::Http, local->endpoint.host, local->endpoint.port.value_or(kDefaultBackendPort)};
    if (const auto* remote = std::get_if<RemoteBackend>(&value)) return remote->url;
    return std::nullopt;
}

std::optional<HostPort> BackendTarget::xmpp() const {
    if (const auto* local = std::get_if<LocalBackend>(&value)) return local->xmpp;
    if (const auto* remote = std::get_if<RemoteBackend>(&value)) return remote->xmpp;
    return std::nullopt;
}

Result<BackendTarget> BackendTarget::from_settings(const storage::BackendTarget& stored) {
    switch (stored.kind) {
    case storage::BackendKind::Embedded:
        return BackendTarget{EmbeddedBackend{}};
    case storage::BackendKind::Local:
        return BackendTarget{LocalBackend{stored.local.endpoint, stored.local.xmpp}};
    case storage::BackendKind::Remote:
        break;
    }
    if (!stored.remote) return invalid_input(msg::kRemoteAddressMissing).fail();
    const storage::RemoteBackendAddress& remote = *stored.remote;
    BackendUrl url{to_url_scheme(remote.scheme), remote.endpoint.host, remote.endpoint.port.value_or(kDefaultBackendPort)};
    return BackendTarget{RemoteBackend{std::move(url), remote.xmpp}};
}

void BackendTarget::apply_to(storage::BackendTarget& stored) const {
    stored.kind = kind();
    if (const auto* local = std::get_if<LocalBackend>(&value)) {
        stored.local = storage::LocalBackendAddress{local->endpoint, local->xmpp};
    } else if (const auto* remote = std::get_if<RemoteBackend>(&value)) {
        stored.remote = storage::RemoteBackendAddress{to_stored_scheme(remote->url.scheme), remote->url.endpoint(),
                                                      remote->xmpp};
    }
}

}  // namespace reboot::backend
