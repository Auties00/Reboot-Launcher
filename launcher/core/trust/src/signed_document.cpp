#include "reboot/trust/signed_document.hpp"

#include <utility>

#include "hex.hpp"

namespace reboot::trust {

namespace {

constexpr std::string_view kScheme = "ed25519 ";
constexpr std::size_t kKeyIdDigits = 16;

}  // namespace

std::expected<SignedDocument, TrustError> make_signed_document(SignedDocumentKind kind, std::vector<u8> body,
                                                               std::string_view signature_file) {
    const auto malformed = [kind] {
        return std::unexpected(TrustError{.code = TrustErrorCode::SignatureMalformed, .document = kind});
    };
    std::string_view text = signature_file;
    if (text.ends_with('\n')) text.remove_suffix(1);
    // A .sig checked out with CRLF line endings.
    if (text.ends_with('\r')) text.remove_suffix(1);
    if (!text.starts_with(kScheme)) return malformed();
    text.remove_prefix(kScheme.size());

    const std::size_t space = text.find(' ');
    if (space != kKeyIdDigits) return malformed();
    const std::string_view key_id = text.substr(0, space);
    // key_id_of is lowercase, so an uppercase id is a signer bug rather than an unknown key.
    if (!is_lower_hex(key_id)) return malformed();
    const auto signature = decode_hex<kEd25519SignatureSize>(text.substr(space + 1));
    if (!signature) return malformed();

    return SignedDocument{.body = std::move(body), .signature = *signature, .key_id = std::string(key_id)};
}

}  // namespace reboot::trust
