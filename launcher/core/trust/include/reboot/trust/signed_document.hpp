#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/types.hpp"
#include "reboot/trust/signed_document_kind.hpp"
#include "reboot/trust/trust_error.hpp"

namespace reboot::trust {

inline constexpr std::size_t kEd25519SignatureSize = 64;

using Ed25519Signature = std::array<u8, kEd25519SignatureSize>;

struct SignedDocument {
    std::vector<u8> body;
    Ed25519Signature signature{};
    std::string key_id;
};

// Pairs a body with its detached ".sig" file: "ed25519 <16 lowercase hex key_id> <128 hex signature>",
// optionally followed by one newline. `kind` only labels a SignatureMalformed error.
[[nodiscard]] std::expected<SignedDocument, TrustError> make_signed_document(SignedDocumentKind kind,
                                                                            std::vector<u8> body,
                                                                            std::string_view signature_file);

}  // namespace reboot::trust
