#pragma once

#include <memory>

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/net.hpp"

namespace rb::ports {
class ISystemInfo;
}

namespace rb::net {

// Capabilities: none; carries the rbsb/1 browse, join and publish connections.
// MsQuic with OpenSSL on every OS, linked at build time (app-local msquic.dll on Windows, the
// signed universal library on macOS). QuicConnectOptions.ipv4_only pins the address family to
// IPv4, which host connections use. The option's ca_bundle, else the one ISystemInfo probed, is
// MsQuic's CA file; otherwise the system store applies. The handshake is bounded by the
// QuicConnect deadline. `remote` dials one resolved address with `host` kept as the TLS server
// name, and `keepalive` sets MsQuic's keep-alive interval. Send buffers are wiped once sent.
class MsQuicTransport final : public ports::IQuicTransport {
public:
    // Opens the MsQuic API and one registration for the process; fails with net.quic_unavailable
    // when MsQuic refuses to start on this system.
    [[nodiscard]] static Result<std::unique_ptr<MsQuicTransport>> create(const ports::ISystemInfo& system);
    // Closes every connection, then the registration; blocks until MsQuic's callbacks are done.
    ~MsQuicTransport() override;
    MsQuicTransport(const MsQuicTransport&) = delete;
    MsQuicTransport& operator=(const MsQuicTransport&) = delete;

    Result<std::unique_ptr<ports::IQuicConnection>> open_connection(const ports::QuicConnectOptions& options,
                                                                    ports::QuicCallbacks callbacks) override;

private:
    struct Impl;
    explicit MsQuicTransport(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::net
