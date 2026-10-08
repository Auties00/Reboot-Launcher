#pragma once

#include <expected>

#include "reboot/trust/key_ring.hpp"
#include "reboot/trust/signed_document.hpp"
#include "reboot/trust/trust_error.hpp"

namespace reboot::trust {

// Ed25519 through OpenSSL EVP over signature_context(ring.kind()) + body. Re-run on every
// load of a cached copy. Decisions: archive-extraction-download, release-pipeline, update-mechanism.
[[nodiscard]] std::expected<void, TrustError> verify_signed(const KeyRing& ring, const SignedDocument& document);

}  // namespace reboot::trust
