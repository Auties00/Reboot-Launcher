#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "net_test_support.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/net/http_error.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_random.hpp"

using namespace reboot;
using namespace reboot::net;
using namespace std::chrono_literals;
using reboot::net::test::bytes_of;
using reboot::testing::FakeHttpResponse;

namespace {

struct Fixture {
    explicit Fixture(std::vector<storage::UpstreamTlsMemory> loaded = {})
        : tls(std::move(loaded), [this](std::vector<storage::UpstreamTlsMemory> records) { persisted.push_back(std::move(records)); }) {}

    testing::DeterministicRuntime runtime;
    testing::FakeHttpTransport transport{runtime.strand(), runtime.clock()};
    testing::FakeRandom random{7};
    std::vector<std::vector<storage::UpstreamTlsMemory>> persisted;
    HostTlsMemory tls;
    std::unique_ptr<HttpClient> client = std::make_unique<HttpClient>(transport, tls, runtime.strand(), runtime.timers(), random);

    std::optional<Result<HttpResponse>> result;
    int calls = 0;

    UniqueFunction<void(Result<HttpResponse>)> capture() {
        return [this](Result<HttpResponse> response) {
            ++calls;
            result = std::move(response);
        };
    }

    void send(HttpRequest request, CancelToken token = {}) { REQUIRE(client->send(std::move(request), std::move(token), capture())); }
};

HttpRequest request_to(std::string url, HttpMethod method = HttpMethod::Get) {
    HttpRequest request;
    request.method = method;
    request.url = std::move(url);
    request.retry.jitter_percent = 0;
    return request;
}

FakeHttpResponse answer(u32 status, std::string body = {}, std::vector<ports::HttpHeader> headers = {}) {
    FakeHttpResponse response;
    response.status = status;
    response.body = bytes_of(body);
    response.headers = std::move(headers);
    return response;
}

FakeHttpResponse failure(Diagnostic error) {
    FakeHttpResponse response;
    response.error = std::move(error);
    return response;
}

Diagnostic connect_failed() { return to_diagnostic(HttpError{.code = HttpErrorCode::Connect, .host = "api.test"}); }

}  // namespace

TEST_CASE("a GET returns status, headers and body, and HostTlsMemory learns the https host", "[net][http]") {
    Fixture f;
    f.transport.route("GET", "https://api.test/v1", answer(200, "hello", {{"X-Kind", "greeting"}}));
    f.send(request_to("https://api.test/v1"));
    f.runtime.run_until_idle();

    REQUIRE(f.calls == 1);
    REQUIRE(f.result->has_value());
    const HttpResponse& response = **f.result;
    CHECK(response.status == 200);
    CHECK(response.body == bytes_of("hello"));
    REQUIRE(response.header("x-kind") != nullptr);
    CHECK(*response.header("x-kind") == "greeting");
    REQUIRE(f.persisted.size() == 1);
    CHECK(f.persisted.back().front().host == "api.test");
    CHECK(f.persisted.back().front().https_seen);

    const std::vector<ports::HttpRequest> sent = f.transport.requests();
    REQUIRE(sent.size() == 1);
    CHECK(sent.front().connect_timeout == kHttpConnectTimeout);
    CHECK(sent.front().total_timeout == default_deadline(OpKind::HttpSmall));
}

TEST_CASE("idempotent requests retry 5xx after a doubling delay; others return the status", "[net][http]") {
    Fixture f;
    f.transport.route_sequence("GET", "https://api.test/r", {answer(503), answer(502), answer(200, "ok")});
    f.send(request_to("https://api.test/r"));
    f.runtime.run_until_idle();
    CHECK(f.transport.requests().size() == 1);
    f.runtime.advance(999ms);
    CHECK(f.transport.requests().size() == 1);
    f.runtime.advance(1ms);
    CHECK(f.transport.requests().size() == 2);
    f.runtime.advance(1999ms);
    CHECK(f.transport.requests().size() == 2);
    f.runtime.advance(1ms);
    REQUIRE(f.calls == 1);
    CHECK((*f.result)->status == 200);

    Fixture post;
    post.transport.route("POST", "https://api.test/r", answer(503));
    post.send(request_to("https://api.test/r", HttpMethod::Post));
    post.runtime.advance(10s);
    REQUIRE(post.calls == 1);
    CHECK((*post.result)->status == 503);
    CHECK(post.transport.requests().size() == 1);
}

TEST_CASE("the last attempt returns its status; Retry-After replaces the delay up to its cap", "[net][http]") {
    Fixture f;
    f.transport.route("GET", "https://api.test/busy", answer(503));
    HttpRequest request = request_to("https://api.test/busy");
    request.retry.max_attempts = 2;
    f.send(std::move(request));
    f.runtime.advance(5s);
    REQUIRE(f.calls == 1);
    CHECK((*f.result)->status == 503);
    CHECK(f.transport.requests().size() == 2);

    Fixture asked;
    asked.transport.route_sequence("GET", "https://api.test/slow", {answer(429, {}, {{"Retry-After", " 5 "}}), answer(200)});
    asked.send(request_to("https://api.test/slow"));
    asked.runtime.advance(4999ms);
    CHECK(asked.transport.requests().size() == 1);
    asked.runtime.advance(1ms);
    CHECK((*asked.result)->status == 200);

    Fixture too_long;
    too_long.transport.route("GET", "https://api.test/later", answer(429, {}, {{"Retry-After", "120"}}));
    too_long.send(request_to("https://api.test/later"));
    too_long.runtime.run_until_idle();
    REQUIRE(too_long.calls == 1);
    CHECK((*too_long.result)->status == 429);
}

TEST_CASE("retryable transport failures retry, then the last failure is returned", "[net][http]") {
    Fixture f;
    f.transport.route("GET", "https://api.test/down", failure(connect_failed()));
    f.send(request_to("https://api.test/down"));
    f.runtime.advance(10s);
    REQUIRE(f.calls == 1);
    REQUIRE_FALSE(f.result->has_value());
    CHECK(f.result->error().id == "net.connect_failed");
    CHECK(f.transport.requests().size() == 3);

    Fixture tls;
    tls.transport.route("GET", "https://api.test/cert", failure(to_diagnostic(HttpError{.code = HttpErrorCode::Tls, .host = "api.test"})));
    tls.send(request_to("https://api.test/cert"));
    tls.runtime.advance(10s);
    REQUIRE_FALSE(tls.result->has_value());
    CHECK(tls.result->error().id == "net.tls_failed");
    CHECK(tls.transport.requests().size() == 1);
    CHECK(tls.persisted.empty());
}

TEST_CASE("a slow answer fails with the connect timeout the request carried", "[net][http]") {
    Fixture f;
    FakeHttpResponse slow = answer(200);
    slow.delay = 15s;
    f.transport.route("GET", "https://api.test/slow", slow);
    HttpRequest request = request_to("https://api.test/slow");
    request.retry = kNoRetry;
    f.send(std::move(request));
    f.runtime.advance(11s);
    REQUIRE(f.calls == 1);
    REQUIRE_FALSE(f.result->has_value());
    CHECK(f.result->error().id == "net.connect_timeout");
}

TEST_CASE("a body past max_body fails with net.response_too_large and is not retried", "[net][http]") {
    Fixture f;
    FakeHttpResponse big = answer(200, std::string(64, 'x'));
    big.chunk_size = 16;
    f.transport.route("GET", "https://api.test/big", big);
    HttpRequest request = request_to("https://api.test/big");
    request.max_body = 40;
    f.send(std::move(request));
    f.runtime.advance(5s);
    REQUIRE(f.calls == 1);
    REQUIRE_FALSE(f.result->has_value());
    CHECK(f.result->error().id == "net.response_too_large");
    CHECK(f.transport.requests().size() == 1);
}

TEST_CASE("cancelling completes once with net.request_cancelled, in flight or between attempts", "[net][http][race]") {
    Fixture f;
    FakeHttpResponse slow = answer(200, "late");
    slow.delay = 2s;
    f.transport.route("GET", "https://api.test/c", slow);
    CancelSource source;
    f.send(request_to("https://api.test/c"), source.token());
    f.runtime.run_until_idle();
    source.cancel(CancelReason::User);
    f.runtime.advance(5s);
    REQUIRE(f.calls == 1);
    REQUIRE_FALSE(f.result->has_value());
    CHECK(f.result->error().id == "net.request_cancelled");
    CHECK(f.result->error().kind == ErrorKind::Cancelled);
    CHECK(f.transport.in_flight() == 0);

    Fixture waiting;
    waiting.transport.route("GET", "https://api.test/w", answer(503));
    CancelSource during_wait;
    waiting.send(request_to("https://api.test/w"), during_wait.token());
    waiting.runtime.advance(500ms);
    during_wait.cancel(CancelReason::Shutdown);
    waiting.runtime.advance(10s);
    REQUIRE(waiting.calls == 1);
    CHECK(waiting.result->error().id == "net.request_cancelled");
    CHECK(waiting.transport.requests().size() == 1);

    Fixture early;
    CancelSource before;
    before.cancel(CancelReason::User);
    early.send(request_to("https://api.test/e"), before.token());
    early.runtime.run_until_idle();
    REQUIRE(early.calls == 1);
    CHECK(early.result->error().id == "net.request_cancelled");
    CHECK(early.transport.requests().empty());
}

TEST_CASE("a cancel racing the answer still completes exactly once", "[net][http][race]") {
    Fixture f;
    f.transport.route("GET", "https://api.test/race", answer(200, "x"));
    CancelSource source;
    f.send(request_to("https://api.test/race"), source.token());
    // The answer and the cancel are both queued before either runs.
    source.cancel(CancelReason::User);
    f.runtime.advance(1s);
    CHECK(f.calls == 1);
}

TEST_CASE("bad URLs, unbounded limits and refused schemes fail synchronously", "[net][http]") {
    Fixture f;
    const Result<void> bad = f.client->send(request_to("ftp://x/"), {}, f.capture());
    REQUIRE_FALSE(bad);
    CHECK(bad.error().id == "net.invalid_url");

    HttpRequest unbounded = request_to("https://api.test/");
    unbounded.limits = HttpLimits{kHttpConnectTimeout, std::nullopt, std::nullopt};
    CHECK(f.client->send(unbounded, {}, f.capture()).error().id == "net.request_unbounded");
    unbounded.limits = HttpLimits{0ms, 5s, std::nullopt};
    CHECK(f.client->send(unbounded, {}, f.capture()).error().id == "net.request_unbounded");

    CHECK(f.client->send(request_to("http://plain.test/"), {}, f.capture()).error().id == "net.plain_http_needs_consent");
    f.tls.remember_http_acknowledged("plain.test");
    f.transport.route("GET", "http://plain.test/", answer(200));
    CHECK(f.client->send(request_to("http://plain.test/"), {}, f.capture()).has_value());
    CHECK(f.client->send(request_to("http://127.0.0.1:3551/"), {}, f.capture()).has_value());

    f.tls.remember_https("secure.test");
    CHECK(f.client->send(request_to("http://secure.test/"), {}, f.capture()).error().id == "net.https_downgrade_refused");
    f.runtime.run_until_idle();
    CHECK(f.transport.requests().size() == 2);
}

TEST_CASE("send_secret appends secret headers, replaces the body and returns SecretBytes", "[net][http]") {
    Fixture f;
    f.transport.route("POST", "https://auth.test/token", answer(200, "{\"access_token\":\"t\"}"));
    HttpRequest request = request_to("https://auth.test/token", HttpMethod::Post);
    request.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
    request.body = bytes_of("public");
    HttpSecrets secrets;
    secrets.headers.push_back({"Authorization", SecretString(std::string("basic abc"))});
    secrets.body = SecretBytes(bytes_of("grant_type=password&password=hunter2"));
    std::optional<Result<SecretHttpResponse>> result;
    REQUIRE(f.client->send_secret(std::move(request), std::move(secrets), {},
                                  [&](Result<SecretHttpResponse> response) { result = std::move(response); }));
    f.runtime.run_until_idle();

    REQUIRE(result);
    REQUIRE(result->has_value());
    CHECK((*result)->status == 200);
    CHECK((*result)->body.reveal() == bytes_of("{\"access_token\":\"t\"}"));
    const ports::HttpRequest sent = f.transport.requests().front();
    REQUIRE(sent.headers.size() == 2);
    CHECK(sent.headers[0].name == "Content-Type");
    CHECK(sent.headers[1].name == "Authorization");
    CHECK(sent.headers[1].value == "basic abc");
    CHECK(sent.body == bytes_of("grant_type=password&password=hunter2"));
    CHECK(sent.method == "POST");
}

TEST_CASE("stream passes callbacks through without retry and remembers the https host", "[net][http]") {
    Fixture f;
    FakeHttpResponse body = answer(200, "abcdef");
    body.chunk_size = 2;
    f.transport.route("GET", "https://dl.test/file", body);
    std::string received;
    std::optional<Result<ports::HttpStatus>> done;
    u32 status = 0;
    ports::HttpCallbacks callbacks;
    callbacks.on_headers = [&](ports::HttpStatus s, const std::vector<ports::HttpHeader>&) { status = s.code; };
    callbacks.on_body_chunk = [&](std::span<const u8> chunk) {
        received.append(chunk.begin(), chunk.end());
        return true;
    };
    callbacks.on_done = [&](Result<ports::HttpStatus> result) { done = std::move(result); };
    HttpRequest request = request_to("https://dl.test/file");
    request.kind = HttpKind::Download;
    REQUIRE(f.client->stream(std::move(request), std::move(callbacks), {}));
    f.runtime.run_until_idle();

    CHECK(status == 200);
    CHECK(received == "abcdef");
    REQUIRE(done);
    CHECK(done->has_value());
    CHECK_FALSE(f.persisted.empty());
    const ports::HttpRequest sent = f.transport.requests().front();
    CHECK(sent.total_timeout == 0ms);
    REQUIRE(sent.stall);
    CHECK(sent.stall->bytes_per_s == kHttpStallFloorBytesPerS);
    CHECK(sent.stall->window == 30s);
}

TEST_CASE("destroying the client cancels its transfers and never calls back", "[net][http]") {
    Fixture f;
    FakeHttpResponse slow = answer(200);
    slow.delay = 3s;
    f.transport.route("GET", "https://api.test/gone", slow);
    f.send(request_to("https://api.test/gone"));
    f.runtime.run_until_idle();
    f.client.reset();
    f.runtime.advance(5s);
    CHECK(f.calls == 0);
    CHECK(f.transport.in_flight() == 0);
}
