#include "security/selfsigned.hpp"

#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <unistd.h>

namespace sb::security {

PemFiles write_self_signed(const std::string& dir) {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_EC_gen("P-256"), &EVP_PKEY_free);
    if (!key) throw std::runtime_error("EC key generation failed");
    std::unique_ptr<X509, decltype(&X509_free)> x(X509_new(), &X509_free);
    X509_set_version(x.get(), 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x.get()), 1);
    X509_gmtime_adj(X509_getm_notBefore(x.get()), -3600);
    X509_gmtime_adj(X509_getm_notAfter(x.get()), 30L * 24 * 3600);
    X509_set_pubkey(x.get(), key.get());
    X509_NAME* name = X509_get_subject_name(x.get());
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0);
    X509_set_issuer_name(x.get(), name);
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, x.get(), x.get(), nullptr, nullptr, 0);
    if (X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1,IP:::1")) {
        X509_add_ext(x.get(), ext, -1);
        X509_EXTENSION_free(ext);
    }
    if (!X509_sign(x.get(), key.get(), EVP_sha256())) throw std::runtime_error("certificate signing failed");

    PemFiles out{dir + "/sb-dev-" + std::to_string(::getpid()) + "-cert.pem", dir + "/sb-dev-" + std::to_string(::getpid()) + "-key.pem"};
    FILE* f = std::fopen(out.cert.c_str(), "wb");
    if (!f || !PEM_write_X509(f, x.get())) throw std::runtime_error("cannot write " + out.cert);
    std::fclose(f);
    f = std::fopen(out.key.c_str(), "wb");
    if (!f || !PEM_write_PrivateKey(f, key.get(), nullptr, nullptr, 0, nullptr, nullptr)) throw std::runtime_error("cannot write " + out.key);
    std::fclose(f);
    return out;
}

}  // namespace sb::security
