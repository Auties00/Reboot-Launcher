#include "reboot/trust/verify_signed.hpp"

#include <openssl/err.h>
#include <openssl/evp.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace reboot::trust {

namespace {

struct PkeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};

struct MdCtxDeleter {
    void operator()(EVP_MD_CTX* context) const noexcept { EVP_MD_CTX_free(context); }
};

// Empties this thread's OpenSSL error queue, so a stale entry never reaches a later caller.
[[nodiscard]] std::string take_openssl_error() {
    std::string text;
    if (const unsigned long code = ERR_get_error(); code != 0) {
        std::array<char, 256> buffer{};
        ERR_error_string_n(code, buffer.data(), buffer.size());
        text = buffer.data();
    }
    ERR_clear_error();
    return text;
}

[[nodiscard]] std::unexpected<TrustError> crypto_failure(SignedDocumentKind kind, const std::string& key_id) {
    return std::unexpected(TrustError{
        .code = TrustErrorCode::CryptoFailure, .document = kind, .key_id = key_id, .detail = take_openssl_error()});
}

}  // namespace

std::expected<void, TrustError> verify_signed(const KeyRing& ring, const SignedDocument& document) {
    const Ed25519PublicKey* key = ring.find(document.key_id);
    if (key == nullptr)
        return std::unexpected(
            TrustError{.code = TrustErrorCode::UnknownKey, .document = ring.kind(), .key_id = document.key_id});

    const std::unique_ptr<EVP_PKEY, PkeyDeleter> public_key(
        EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, key->data(), key->size()));
    const std::unique_ptr<EVP_MD_CTX, MdCtxDeleter> context(EVP_MD_CTX_new());
    if (!public_key || !context ||
        EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, public_key.get()) != 1)
        return crypto_failure(ring.kind(), document.key_id);

    // Pure Ed25519 is one-shot, so the context prefix and the body go in as one buffer.
    const std::string_view prefix = signature_context(ring.kind());
    std::vector<u8> message;
    message.reserve(prefix.size() + document.body.size());
    message.insert(message.end(), prefix.begin(), prefix.end());
    message.insert(message.end(), document.body.begin(), document.body.end());

    const int verified = EVP_DigestVerify(context.get(), document.signature.data(), document.signature.size(),
                                          message.data(), message.size());
    if (verified == 1) return {};
    if (verified < 0) return crypto_failure(ring.kind(), document.key_id);
    ERR_clear_error();
    return std::unexpected(
        TrustError{.code = TrustErrorCode::SignatureInvalid, .document = ring.kind(), .key_id = document.key_id});
}

}  // namespace reboot::trust
