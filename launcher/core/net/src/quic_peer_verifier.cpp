#include "quic_peer_verifier.hpp"

#include <limits>
#include <optional>
#include <utility>

#include <openssl/pkcs7.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include "messages.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/text.hpp"

namespace rb::net {

namespace {

struct StoreFree {
    void operator()(X509_STORE* store) const noexcept { X509_STORE_free(store); }
};
struct X509Free {
    void operator()(X509* certificate) const noexcept { X509_free(certificate); }
};
struct Pkcs7Free {
    void operator()(PKCS7* bundle) const noexcept { PKCS7_free(bundle); }
};
struct ContextFree {
    void operator()(X509_STORE_CTX* context) const noexcept { X509_STORE_CTX_free(context); }
};

[[nodiscard]] bool fits_long(std::span<const u8> bytes) noexcept {
    return bytes.size() <= static_cast<std::size_t>(std::numeric_limits<long>::max());
}

}  // namespace

struct QuicPeerVerifier::Impl {
    std::unique_ptr<X509_STORE, StoreFree> store;
    std::string host;
};

Result<std::unique_ptr<QuicPeerVerifier>> QuicPeerVerifier::create(const NativePath& ca_file, std::string host) {
    auto impl = std::make_unique<Impl>();
    impl->store.reset(X509_STORE_new());
    // OpenSSL takes UTF-8 file names on Windows too.
    if (!impl->store || X509_STORE_load_file(impl->store.get(), display_utf8(ca_file).c_str()) != 1)
        return make_diag(ErrorDomain::Net, kQuicConnectFailed)
            .arg("host", host)
            .detail("the CA bundle " + display_utf8(ca_file) + " holds no usable certificate")
            .fail();
    impl->host = std::move(host);
    return std::unique_ptr<QuicPeerVerifier>(new QuicPeerVerifier(std::move(impl)));
}

QuicPeerVerifier::QuicPeerVerifier(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

QuicPeerVerifier::~QuicPeerVerifier() = default;

bool QuicPeerVerifier::verify(std::span<const u8> certificate, std::span<const u8> chain) const {
    if (certificate.empty() || !fits_long(certificate) || !fits_long(chain)) return false;
    const unsigned char* cursor = certificate.data();
    const std::unique_ptr<X509, X509Free> leaf(d2i_X509(nullptr, &cursor, static_cast<long>(certificate.size())));
    if (!leaf) return false;
    std::unique_ptr<PKCS7, Pkcs7Free> bundle;
    STACK_OF(X509)* intermediates = nullptr;
    if (!chain.empty()) {
        cursor = chain.data();
        bundle.reset(d2i_PKCS7(nullptr, &cursor, static_cast<long>(chain.size())));
        if (bundle && PKCS7_type_is_signed(bundle.get()) && bundle->d.sign != nullptr) intermediates = bundle->d.sign->cert;
    }
    const std::unique_ptr<X509_STORE_CTX, ContextFree> context(X509_STORE_CTX_new());
    if (!context || X509_STORE_CTX_init(context.get(), impl_->store.get(), leaf.get(), intermediates) != 1) return false;
    X509_VERIFY_PARAM* param = X509_STORE_CTX_get0_param(context.get());
    X509_VERIFY_PARAM_set_purpose(param, X509_PURPOSE_SSL_SERVER);
    const bool named = IpAddress::parse(impl_->host) ? X509_VERIFY_PARAM_set1_ip_asc(param, impl_->host.c_str()) == 1
                                                     : X509_VERIFY_PARAM_set1_host(param, impl_->host.c_str(), 0) == 1;
    return named && X509_verify_cert(context.get()) == 1;
}

}  // namespace rb::net
