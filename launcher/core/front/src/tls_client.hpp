#pragma once

#include <array>
#include <memory>
#include <optional>

#include <boost/asio/ssl/context.hpp>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/front/upstream_origin.hpp"

namespace rb::front {

// Every upstream TLS connection's context: TLS 1.2 or later, chains checked against `ca_bundle`, else
// the system store.
[[nodiscard]] std::unique_ptr<boost::asio::ssl::context> make_upstream_tls_context(
    const std::optional<NativePath>& ca_bundle);

// SNI plus, unless `pinned`, chain and host-name verification for `origin`.
void prepare_tls(SSL* ssl, const UpstreamOrigin& origin, bool pinned);

// The SHA-256 of the peer's leaf certificate in DER, compared in constant time.
[[nodiscard]] bool certificate_matches(SSL* ssl, const std::array<u8, 32>& pin);

}  // namespace rb::front
