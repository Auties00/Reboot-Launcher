#include "tls_client.hpp"

#include <span>
#include <string>
#include <vector>

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "reboot/foundation/log.hpp"
#include "reboot/foundation/sha256.hpp"

namespace reboot::front {

std::unique_ptr<boost::asio::ssl::context> make_upstream_tls_context(const std::optional<NativePath>& ca_bundle) {
    auto context = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tls_client);
    SSL_CTX* native = context->native_handle();
    SSL_CTX_set_min_proto_version(native, TLS1_2_VERSION);
    SSL_CTX_set_verify(native, SSL_VERIFY_PEER, nullptr);
    bool loaded = false;
    if (ca_bundle) {
        const std::u8string path = ca_bundle->u8string();
        loaded = SSL_CTX_load_verify_file(native, reinterpret_cast<const char*>(path.c_str())) == 1;
    } else {
#ifdef _WIN32
        loaded = SSL_CTX_load_verify_store(native, "org.openssl.winstore:") == 1;
#else
        loaded = SSL_CTX_set_default_verify_paths(native) == 1;
#endif
    }
    // Without trust anchors every unpinned https upstream fails verification, which is the safe outcome.
    if (!loaded) REBOOT_LOG_WARN(Net, "front: the system certificate store could not be loaded");
    return context;
}

void prepare_tls(SSL* ssl, const UpstreamOrigin& origin, bool pinned) {
    const bool ip_literal = IpAddress::parse(origin.host).has_value();
    if (!ip_literal)
        SSL_ctrl(ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name, const_cast<char*>(origin.host.c_str()));
    if (pinned) {
        SSL_set_verify(ssl, SSL_VERIFY_NONE, nullptr);
        return;
    }
    SSL_set_verify(ssl, SSL_VERIFY_PEER, nullptr);
    X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
    if (ip_literal) {
        X509_VERIFY_PARAM_set1_ip_asc(param, origin.host.c_str());
    } else {
        X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        X509_VERIFY_PARAM_set1_host(param, origin.host.c_str(), origin.host.size());
    }
}

bool certificate_matches(SSL* ssl, const std::array<u8, 32>& pin) {
    X509* certificate = SSL_get1_peer_certificate(ssl);
    if (certificate == nullptr) return false;
    const int size = i2d_X509(certificate, nullptr);
    bool matches = false;
    if (size > 0) {
        std::vector<u8> der(static_cast<std::size_t>(size));
        unsigned char* out = der.data();
        if (i2d_X509(certificate, &out) == size) {
            const std::array<u8, 32> digest = sha256(der);
            matches = constant_time_equal(digest, pin);
        }
    }
    X509_free(certificate);
    return matches;
}

}  // namespace reboot::front
