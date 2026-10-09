#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/net.hpp"

namespace rb::testing {

struct FakeHttpResponse {
    u32 status = 200;
    std::vector<ports::HttpHeader> headers;
    std::vector<u8> body;
    // 0 sends the body as one chunk.
    std::size_t chunk_size = 0;
    // Before the headers, on the executor's clock.
    std::chrono::milliseconds delay{0};
    // The body stops after this many bytes; the request's StallPolicy or a cancel then ends it.
    std::optional<std::size_t> stall_after;
    // Fails instead of answering, as a DNS, connect or TLS error would.
    std::optional<Diagnostic> error;
    // Answers "Range: bytes=N-" with 206 and the tail of `body`, for resumable downloads.
    bool ranges = false;
};

// Covers no capability ids (decision testing-strategy).
// IHttpTransport over canned responses posted to `deliver_on` on `clock`'s time; on_done runs exactly
// once per perform(), and an unrouted request fails with testing.no_http_route. A request's
// connect_timeout bounds the delay, total_timeout the whole answer and its StallPolicy a stalled body,
// failing with the net.connect_timeout, net.request_timeout and net.transfer_stalled the real one uses.
class FakeHttpTransport final : public ports::IHttpTransport {
public:
    FakeHttpTransport(Executor& deliver_on, const IClock& clock);
    ~FakeHttpTransport() override;
    FakeHttpTransport(const FakeHttpTransport&) = delete;
    FakeHttpTransport& operator=(const FakeHttpTransport&) = delete;

    void perform(ports::HttpRequest request, ports::HttpCallbacks callbacks, CancelToken token) override;

    // Exact method and URL; the newest matching route wins.
    void route(std::string method, std::string url, FakeHttpResponse response);
    // Each request takes the next response; the last one repeats.
    void route_sequence(std::string method, std::string url, std::vector<FakeHttpResponse> responses);
    // Every URL starting with `url_prefix`, answered from the request.
    void route_handler(std::string method, std::string url_prefix,
                       UniqueFunction<FakeHttpResponse(const ports::HttpRequest&)> handler);
    void clear_routes();

    // Every request performed, in order.
    [[nodiscard]] std::vector<ports::HttpRequest> requests() const;
    [[nodiscard]] std::size_t in_flight() const;

private:
    struct State;
    // Shared with posted deliveries and cancel registrations.
    std::shared_ptr<State> state_;
};

}  // namespace rb::testing
