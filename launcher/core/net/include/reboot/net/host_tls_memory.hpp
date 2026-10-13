#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "reboot/base/diag.hpp"
#include "reboot/base/function.hpp"
#include "reboot/foundation/flat_map.hpp"
#include "reboot/net/url_scheme.hpp"
#include "reboot/storage/state_document.hpp"

namespace rb::net {

// Capabilities: matchmaking-networking.+72, matchmaking-networking.+84.
// Strand-only. A host once seen on https is never reached over http again, acknowledged or not.
// Plain http to any other host needs a remembered ConfirmUnencryptedUpstream answer. Only the
// literal loopback hosts 127.0.0.0/8, ::1 and "localhost" are exempt, never a name that merely
// resolves to loopback: their traffic cannot leave the machine. Hosts are compared lowercase,
// without port.
class HostTlsMemory {
public:
    // `persist` receives the full record set after every change; the engine writes it into
    // StateDocument::upstream_tls.
    HostTlsMemory(std::vector<storage::UpstreamTlsMemory> loaded,
                  UniqueFunction<void(std::vector<storage::UpstreamTlsMemory>)> persist);

    // Fails with net.https_downgrade_refused or net.plain_http_needs_consent.
    [[nodiscard]] Result<void> check(UrlScheme scheme, std::string_view host) const;

    // The hosts check() lets through over plain http, loopback aside.
    [[nodiscard]] std::vector<std::string> plain_http_hosts() const;
    // The hosts check() refuses over plain http as a downgrade.
    [[nodiscard]] std::vector<std::string> https_hosts() const;

    void remember_https(std::string_view host);
    void remember_http_acknowledged(std::string_view host);

    [[nodiscard]] std::vector<storage::UpstreamTlsMemory> records() const;

private:
    FlatMap<std::string, storage::UpstreamTlsMemory> records_;
    UniqueFunction<void(std::vector<storage::UpstreamTlsMemory>)> persist_;
};

}  // namespace rb::net
