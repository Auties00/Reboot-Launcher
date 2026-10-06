#include "security/crypto.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <stdexcept>

namespace sb::security {

Digest sha256(std::span<const u8> data) {
    Digest out{};
    unsigned int len = 0;
    if (EVP_Digest(data.data(), data.size(), out.data(), &len, EVP_sha256(), nullptr) != 1 || len != out.size())
        throw std::runtime_error("EVP_Digest failed");
    return out;
}

Digest hmac_sha256(std::span<const u8> key, std::span<const u8> data) {
    return hmac_sha256(key, {data});
}

Digest hmac_sha256(std::span<const u8> key, std::initializer_list<std::span<const u8>> parts) {
    Digest out{};
    // Fetching is expensive; the fetched algorithm is immutable and shareable across threads.
    static EVP_MAC* const mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    EVP_MAC_CTX* ctx = mac ? EVP_MAC_CTX_new(mac) : nullptr;
    char digest_name[] = "SHA256";
    const OSSL_PARAM params[] = {OSSL_PARAM_construct_utf8_string("digest", digest_name, 0), OSSL_PARAM_construct_end()};
    std::size_t len = 0;
    bool ok = ctx && EVP_MAC_init(ctx, key.data(), key.size(), params) == 1;
    for (auto p : parts) ok = ok && EVP_MAC_update(ctx, p.data(), p.size()) == 1;
    ok = ok && EVP_MAC_final(ctx, out.data(), &len, out.size()) == 1 && len == out.size();
    EVP_MAC_CTX_free(ctx);
    if (!ok) throw std::runtime_error("HMAC failed");
    return out;
}

void random_bytes(std::span<u8> out) {
    if (RAND_bytes(out.data(), static_cast<int>(out.size())) != 1) throw std::runtime_error("RAND_bytes failed");
}

bool equal_ct(std::span<const u8> a, std::span<const u8> b) noexcept {
    return a.size() == b.size() && CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

}  // namespace sb::security
