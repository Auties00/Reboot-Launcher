#pragma once

#include <memory>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/net/http_request.hpp"
#include "reboot/net/http_response.hpp"
#include "reboot/net/http_secrets.hpp"
#include "reboot/net/secret_http_response.hpp"
#include "reboot/ports/net.hpp"

namespace rb {
class Executor;
class IRandom;
class TimerService;
}  // namespace rb

namespace rb::net {

class HostTlsMemory;

// Capabilities: matchmaking-networking.http-timeouts, matchmaking-networking.+72, matchmaking-networking.+84.
// The engine's one HTTP client. Strand-only. Every call is bounded by its kind's limits and
// cancellable; TLS is always verified by the transport; HostTlsMemory gates the scheme and learns
// https hosts from successful responses.
class HttpClient {
public:
    HttpClient(ports::IHttpTransport& transport, HostTlsMemory& tls, Executor& strand, TimerService& timers,
               IRandom& random);
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Buffers up to request.max_body and retries per request.retry on DNS, connect and timeout
    // failures, 429 and 5xx. Any other status is returned, not failed. Fails synchronously on a
    // bad URL, unbounded limits (net.request_unbounded) or a scheme HostTlsMemory refuses. `done`
    // runs on the strand, exactly once.
    Result<void> send(HttpRequest request, CancelToken token, UniqueFunction<void(Result<HttpResponse>)> done);

    // send() for credential exchanges: `secrets` join the request and the body comes back as
    // SecretBytes, buffered without an intermediate plain copy.
    Result<void> send_secret(HttpRequest request, HttpSecrets secrets, CancelToken token,
                             UniqueFunction<void(Result<SecretHttpResponse>)> done);

    // No retry and no buffering, for the downloader. Validates like send(); `callbacks` run on the
    // transport's thread.
    Result<void> stream(HttpRequest request, ports::HttpCallbacks callbacks, CancelToken token);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::net
