#include "reboot/net/host_tls_memory.hpp"

#include <optional>
#include <utility>

#include "reboot/foundation/net_types.hpp"
#include "reboot/net/http_error.hpp"
#include "url.hpp"

namespace reboot::net {

namespace {

[[nodiscard]] bool is_literal_loopback(const std::string& host) {
    if (host == "localhost") return true;
    const std::optional<IpAddress> address = IpAddress::parse(host);
    return address && address->is_loopback();
}

}  // namespace

HostTlsMemory::HostTlsMemory(std::vector<storage::UpstreamTlsMemory> loaded,
                             UniqueFunction<void(std::vector<storage::UpstreamTlsMemory>)> persist)
    : persist_(std::move(persist)) {
    for (storage::UpstreamTlsMemory& record : loaded) {
        std::string host = normalize_host(record.host);
        if (host.empty()) continue;
        storage::UpstreamTlsMemory& entry = records_[host];
        entry.host = std::move(host);
        entry.https_seen = entry.https_seen || record.https_seen;
        entry.http_acknowledged = entry.http_acknowledged || record.http_acknowledged;
    }
}

Result<void> HostTlsMemory::check(UrlScheme scheme, std::string_view host) const {
    if (scheme == UrlScheme::Https) return {};
    const std::string key = normalize_host(host);
    if (is_literal_loopback(key)) return {};
    const auto it = records_.find(key);
    if (it != records_.end() && it->second.https_seen)
        return std::unexpected(to_diagnostic(HttpError{.code = HttpErrorCode::DowngradeRefused, .host = key}));
    if (it != records_.end() && it->second.http_acknowledged) return {};
    return std::unexpected(to_diagnostic(HttpError{.code = HttpErrorCode::PlainHttpNeedsConsent, .host = key}));
}

void HostTlsMemory::remember_https(std::string_view host) {
    std::string key = normalize_host(host);
    if (key.empty() || is_literal_loopback(key)) return;
    storage::UpstreamTlsMemory& entry = records_[key];
    if (entry.https_seen) return;
    entry.host = std::move(key);
    entry.https_seen = true;
    if (persist_) persist_(records());
}

void HostTlsMemory::remember_http_acknowledged(std::string_view host) {
    std::string key = normalize_host(host);
    if (key.empty() || is_literal_loopback(key)) return;
    storage::UpstreamTlsMemory& entry = records_[key];
    if (entry.http_acknowledged) return;
    entry.host = std::move(key);
    entry.http_acknowledged = true;
    if (persist_) persist_(records());
}

std::vector<storage::UpstreamTlsMemory> HostTlsMemory::records() const {
    std::vector<storage::UpstreamTlsMemory> out;
    out.reserve(records_.size());
    for (const auto& [host, record] : records_) out.push_back(record);
    return out;
}

}  // namespace reboot::net
