#pragma once

#include <memory>
#include <span>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::net {

// A QUIC server's certificate chain checked against one CA file alone, for one host, as OpenSSL
// checks a TLS server. Schannel takes no CA file, so this runs on every OS for an explicit bundle.
// Thread-safe once created; MsQuic calls verify() on its own threads.
class QuicPeerVerifier {
public:
    // Fails with net.quic_connect_failed when the file holds no usable certificate.
    [[nodiscard]] static Result<std::unique_ptr<QuicPeerVerifier>> create(const NativePath& ca_file, std::string host);
    ~QuicPeerVerifier();
    QuicPeerVerifier(const QuicPeerVerifier&) = delete;
    QuicPeerVerifier& operator=(const QuicPeerVerifier&) = delete;

    // `certificate` is DER, `chain` the PKCS#7 bundle MsQuic's portable format carries (may be empty).
    [[nodiscard]] bool verify(std::span<const u8> certificate, std::span<const u8> chain) const;

private:
    struct Impl;
    explicit QuicPeerVerifier(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::net
