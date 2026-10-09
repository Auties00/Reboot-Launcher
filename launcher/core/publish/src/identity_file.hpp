#pragma once

#include <optional>
#include <span>
#include <string>

#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/publish/host_identity.hpp"

namespace reboot::publish {

// The {server_id, token} record of a profile file and of an export.
struct StoredIdentity {
    ServerId server;
    std::optional<HostToken> token;
};

// {"server_id": "<uuid>", "token": "<64 lowercase hex>"}; the token member is left out when absent.
[[nodiscard]] SecretBytes encode_identity(const ServerId& server, const std::optional<HostToken>& token);
// nullopt unless `bytes` is such an object with a non-nil id and, when present, a well-formed token.
[[nodiscard]] std::optional<StoredIdentity> decode_identity(std::span<const u8> bytes);

// The token's text form, which is what a log line or the file would show.
[[nodiscard]] SecretString token_text(const HostToken& token);

}  // namespace reboot::publish
