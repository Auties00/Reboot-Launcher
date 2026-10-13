#include "test_pki.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/x509v3.h>

#include "reboot/foundation/random.hpp"

namespace rb::net::test {

namespace {

struct BioFree {
    void operator()(BIO* bio) const noexcept { BIO_free(bio); }
};
struct Pkcs12Free {
    void operator()(PKCS12* p12) const noexcept { PKCS12_free(p12); }
};

std::shared_ptr<EVP_PKEY> make_key() {
    std::shared_ptr<EVP_PKEY> key(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256"), PkeyFree{});
    REQUIRE(key);
    return key;
}

void add_extension(X509* certificate, X509* issuer, int nid, const char* value) {
    X509V3_CTX context;
    X509V3_set_ctx_nodb(&context);
    X509V3_set_ctx(&context, issuer, certificate, nullptr, nullptr, 0);
    X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &context, nid, value);
    REQUIRE(extension != nullptr);
    REQUIRE(X509_add_ext(certificate, extension, -1) == 1);
    X509_EXTENSION_free(extension);
}

// Self-signed when `issuer` is null.
std::shared_ptr<X509> make_certificate(const char* common_name, EVP_PKEY* key, X509* issuer, EVP_PKEY* issuer_key,
                                       long serial, bool ca) {
    std::shared_ptr<X509> certificate(X509_new(), X509Free{});
    REQUIRE(certificate);
    X509* raw = certificate.get();
    X509_set_version(raw, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(raw), serial);
    X509_gmtime_adj(X509_getm_notBefore(raw), -3600);
    X509_gmtime_adj(X509_getm_notAfter(raw), 86400);
    X509_set_pubkey(raw, key);
    X509_NAME* name = X509_get_subject_name(raw);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(common_name), -1, -1, 0);
    X509* signer = issuer != nullptr ? issuer : raw;
    X509_set_issuer_name(raw, X509_get_subject_name(signer));
    if (ca) {
        add_extension(raw, signer, NID_basic_constraints, "critical,CA:TRUE");
        add_extension(raw, signer, NID_key_usage, "critical,keyCertSign,cRLSign");
        add_extension(raw, signer, NID_subject_key_identifier, "hash");
    } else {
        add_extension(raw, signer, NID_basic_constraints, "critical,CA:FALSE");
        add_extension(raw, signer, NID_key_usage, "critical,digitalSignature");
        add_extension(raw, signer, NID_ext_key_usage, "serverAuth");
        add_extension(raw, signer, NID_subject_alt_name, "DNS:localhost");
        add_extension(raw, signer, NID_authority_key_identifier, "keyid:always");
    }
    REQUIRE(X509_sign(raw, issuer_key != nullptr ? issuer_key : key, EVP_sha256()) > 0);
    return certificate;
}

void write_pem(const NativePath& path, X509* certificate) {
    const std::unique_ptr<BIO, BioFree> bio(BIO_new(BIO_s_mem()));
    REQUIRE(PEM_write_bio_X509(bio.get(), certificate) == 1);
    char* data = nullptr;
    const long size = BIO_get_mem_data(bio.get(), &data);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(data, size);
    REQUIRE(out.good());
}

}  // namespace

TestPki make_test_pki() {
    OsRandom random;
    Result<testing::ScratchDir> dir = testing::ScratchDir::create(random, "reboot-net-pki");
    REQUIRE(dir);
    const std::shared_ptr<EVP_PKEY> ca_key = make_key();
    const std::shared_ptr<X509> ca = make_certificate("Reboot Test CA", ca_key.get(), nullptr, nullptr, 1, true);
    const std::shared_ptr<EVP_PKEY> other_key = make_key();
    const std::shared_ptr<X509> other = make_certificate("Reboot Other CA", other_key.get(), nullptr, nullptr, 2, true);
    std::shared_ptr<EVP_PKEY> server_key = make_key();
    std::shared_ptr<X509> server = make_certificate("localhost", server_key.get(), ca.get(), ca_key.get(), 3, false);

    TestPki out{.dir = std::move(*dir), .ca_file = {}, .other_ca_file = {}, .server_certificate = server,
                .server_key = server_key, .server_pkcs12 = {}};
    out.ca_file = out.dir.path() / "ca.pem";
    out.other_ca_file = out.dir.path() / "other-ca.pem";
    write_pem(out.ca_file, ca.get());
    write_pem(out.other_ca_file, other.get());

    const std::unique_ptr<PKCS12, Pkcs12Free> p12(PKCS12_create(kPkcs12Password, "localhost", server_key.get(), server.get(),
                                                                 nullptr, 0, 0, 0, 0, 0));
    REQUIRE(p12);
    const int size = i2d_PKCS12(p12.get(), nullptr);
    REQUIRE(size > 0);
    out.server_pkcs12.resize(static_cast<std::size_t>(size));
    unsigned char* cursor = out.server_pkcs12.data();
    i2d_PKCS12(p12.get(), &cursor);
    return out;
}

}  // namespace rb::net::test
