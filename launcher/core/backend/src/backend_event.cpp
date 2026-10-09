#include "reboot/backend/backend_event.hpp"

namespace rb::backend {

namespace {

[[nodiscard]] std::size_t diagnostic_bytes(const Diagnostic& diag) noexcept {
    std::size_t bytes = sizeof(Diagnostic) + diag.id.size() + (diag.detail ? diag.detail->size() : 0);
    for (const auto& [name, value] : diag.args) bytes += name.size() + sizeof(value);
    for (const Diagnostic& cause : diag.causes) bytes += diagnostic_bytes(cause);
    return bytes;
}

[[nodiscard]] std::size_t config_bytes(const BackendConfig& config) noexcept {
    std::size_t bytes = 0;
    if (const std::optional<BackendUrl> url = config.target.upstream_url()) bytes += url->host.size();
    if (const std::optional<HostPort> xmpp = config.target.xmpp()) bytes += xmpp->host.size();
    return bytes;
}

}  // namespace

std::size_t BackendEvent::approx_bytes() const noexcept {
    std::size_t bytes = sizeof(BackendEvent) + state.version.size() + config_bytes(state.config);
    if (state.upstream) {
        bytes += state.upstream->origin.size();
        if (state.upstream->websocket) bytes += state.upstream->websocket->host.size();
    }
    if (state.last_error) bytes += diagnostic_bytes(*state.last_error);
    if (state.pending_config) bytes += config_bytes(*state.pending_config);
    return bytes;
}

}  // namespace rb::backend
