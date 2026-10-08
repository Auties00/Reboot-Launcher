#include "reboot/storage/backend_target.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "messages.hpp"

namespace reboot::storage {

namespace {

constexpr std::size_t kMaxHostNameBytes = 253;
constexpr std::size_t kMaxLabelBytes = 63;

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    const auto blank = [](char c) { return c == ' ' || c == '\t'; };
    while (!text.empty() && blank(text.front())) text.remove_prefix(1);
    while (!text.empty() && blank(text.back())) text.remove_suffix(1);
    return text;
}

[[nodiscard]] char to_lower_ascii(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// RFC 1123 on an already lowercased name.
[[nodiscard]] bool is_host_name(std::string_view host) noexcept {
    if (host.empty() || host.size() > kMaxHostNameBytes) return false;
    std::size_t start = 0;
    while (start <= host.size()) {
        const std::size_t dot = std::min(host.find('.', start), host.size());
        const std::string_view label = host.substr(start, dot - start);
        if (label.empty() || label.size() > kMaxLabelBytes || label.front() == '-' || label.back() == '-') return false;
        const bool valid = std::ranges::all_of(label, [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        });
        if (!valid) return false;
        start = dot + 1;
    }
    return true;
}

[[nodiscard]] Result<HostPort> normalize_endpoint(const HostPort& endpoint, Port default_port) {
    std::string host(trim(endpoint.host));
    std::ranges::transform(host, host.begin(), to_lower_ascii);
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
    if (!IpAddress::parse(host) && !is_host_name(host))
        return invalid_input(msg::kInvalidHost).arg("host", endpoint.host).fail();
    return HostPort{std::move(host), endpoint.port.value_or(default_port)};
}

[[nodiscard]] Result<std::optional<HostPort>> normalize_xmpp(const std::optional<HostPort>& xmpp) {
    if (!xmpp) return std::optional<HostPort>{};
    return normalize_endpoint(*xmpp, kDefaultXmppPort).transform([](HostPort valid) {
        return std::optional<HostPort>(std::move(valid));
    });
}

}  // namespace

Result<BackendTarget> BackendTarget::normalize(BackendTarget target) {
    Result<HostPort> local = normalize_endpoint(target.local.endpoint, kDefaultBackendPort);
    if (!local) return std::unexpected(std::move(local.error()));
    target.local.endpoint = std::move(*local);
    Result<std::optional<HostPort>> local_xmpp = normalize_xmpp(target.local.xmpp);
    if (!local_xmpp) return std::unexpected(std::move(local_xmpp.error()));
    target.local.xmpp = std::move(*local_xmpp);

    if (target.remote) {
        Result<HostPort> remote = normalize_endpoint(target.remote->endpoint, kDefaultBackendPort);
        if (!remote) return std::unexpected(std::move(remote.error()));
        target.remote->endpoint = std::move(*remote);
        Result<std::optional<HostPort>> remote_xmpp = normalize_xmpp(target.remote->xmpp);
        if (!remote_xmpp) return std::unexpected(std::move(remote_xmpp.error()));
        target.remote->xmpp = std::move(*remote_xmpp);
    } else if (target.kind == BackendKind::Remote) {
        const std::string_view kind = EnumNames<BackendKind>::kNames[static_cast<std::size_t>(BackendKind::Remote)];
        return invalid_input(msg::kInvalidBackendTarget).arg("kind", kind).fail();
    }
    return target;
}

}  // namespace reboot::storage
