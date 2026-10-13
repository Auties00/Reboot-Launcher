#pragma once

#include <memory>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/x509.h>

#include "reboot/base/native_path.hpp"
#include "reboot/base/types.hpp"
#include "reboot/testing/scratch_dir.hpp"

namespace rb::net::test {

struct X509Free {
    void operator()(X509* certificate) const noexcept { X509_free(certificate); }
};
struct PkeyFree {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};

// A test CA and a server certificate it signed for DNS:localhost only, written as PEM files into a
// scratch directory. `other_ca_file` is a second CA that signed nothing here.
struct TestPki {
    testing::ScratchDir dir;
    NativePath ca_file;
    NativePath other_ca_file;
    std::shared_ptr<X509> server_certificate;
    std::shared_ptr<EVP_PKEY> server_key;
    // The server certificate and key as PKCS#12, protected by kPkcs12Password.
    std::vector<u8> server_pkcs12;
};

inline constexpr const char* kPkcs12Password = "reboot-test";

[[nodiscard]] TestPki make_test_pki();

}  // namespace rb::net::test
