#pragma once

#include <memory>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/ports/net.hpp"

namespace rb::ports {
class ISystemInfo;
}

namespace rb::net {

// Capabilities: matchmaking-networking.http-timeouts, matchmaking-networking.+72, matchmaking-networking.+84.
// libcurl on one transfer thread with a shared connection and DNS cache. TLS per OS:
// - Windows: Schannel with the OS root store.
// - macOS: OpenSSL with USE_APPLE_SECTRUST (curl 8.17 or later), so the keychain decides trust.
// - Linux: OpenSSL with the CA bundle that ISystemInfo probed.
// Peer and host verification are never turned off, and redirects never leave https. Request
// bodies and header values are wiped once a transfer ends and are never logged, since
// HttpClient::send_secret passes credentials through them. Failures are HttpError diagnostics.
class CurlHttpTransport final : public ports::IHttpTransport {
public:
    // Runs curl_global_init once per process and starts the transfer thread.
    [[nodiscard]] static Result<std::unique_ptr<CurlHttpTransport>> create(const ports::ISystemInfo& system);
    // Aborts open transfers; each gets its on_done before the thread stops.
    ~CurlHttpTransport() override;
    CurlHttpTransport(const CurlHttpTransport&) = delete;
    CurlHttpTransport& operator=(const CurlHttpTransport&) = delete;

    void perform(ports::HttpRequest request, ports::HttpCallbacks callbacks, CancelToken token) override;

private:
    struct Impl;
    explicit CurlHttpTransport(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::net
