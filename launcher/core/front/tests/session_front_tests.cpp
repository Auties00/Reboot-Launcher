#include <any>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "front_test_support.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/front/front_answer.hpp"
#include "reboot/front/reboot_headers.hpp"
#include "reboot/front/session_front.hpp"
#include "reboot/front/ticket_exchange.hpp"
#include "reboot/front/unencrypted_upstream_prompt.hpp"

using namespace rb;
using namespace rb::front;
using namespace rb::front::test;
using namespace std::chrono_literals;

namespace {

const SessionId kSession = session_id(1);
const SessionKey kKey = session_key(0x10);
constexpr std::string_view kTicket = "t1cket-T0KEN";
constexpr std::string_view kTicketGrant = "grant_type=password&username=x&password=t1cket-T0KEN";
constexpr std::string_view kTokenPath = "/account/api/oauth/token";
constexpr std::string_view kTicketPath = "/fortnite/api/game/v2/matchmakingservice/ticket/player/abc";

std::string prefix() { return "/s/" + kKey.to_hex(); }

UpstreamOrigin origin(std::string_view url) { return *parse_upstream_origin(url); }

UpstreamOrigin loopback_origin(Port port) { return UpstreamOrigin{net::UrlScheme::Http, "127.0.0.1", port}; }

FrontRoute configured_route(SessionId session, const SessionKey& key, UpstreamOrigin backend,
                            std::optional<TicketBinding> tickets = std::nullopt, bool plain_consented = false,
                            std::optional<std::array<u8, 32>> pin = std::nullopt) {
    FrontRoute route{session, key, ConfiguredUpstream{std::move(backend), pin, plain_consented}, std::nullopt};
    if (tickets)
        route.tickets.emplace(SecretString{std::string(kTicket)},
                              UpstreamLogin{"player@example.com", SecretString{"p&ss w"}}, *tickets);
    return route;
}

std::string header(const Reply& reply, std::string_view name) {
    const auto field = reply.headers.find(name);
    return field == reply.headers.end() ? std::string() : std::string(field->value());
}

const UserRequest* pending_prompt(FrontHarness& h) {
    static std::vector<UserRequest> pending;
    pending = h.runtime.requests().pending();
    for (const UserRequest& request : pending)
        if (request.kind == UserRequestKind::ConfirmUnencryptedUpstream) return &request;
    return nullptr;
}

}  // namespace

TEST_CASE("start binds loopback and origin carries the session key", "[front][session]") {
    FrontHarness h;
    CHECK(h.front.origin(kKey).error().id == "front.not_started");
    CHECK(h.front.add_route(h.embedded_route(kSession, kKey)).error().id == "front.not_started");
    CHECK(!h.front.port());

    const Result<Port> port = h.front.start();
    REQUIRE(port);
    CHECK(port->value != 0);
    CHECK(h.front.start().value() == *port);
    CHECK(h.front.origin(kKey).value() == "http://127.0.0.1:" + std::to_string(port->value) + "/s/" + kKey.to_hex() + "/");
}

TEST_CASE("routes are unique per session and per key", "[front][session]") {
    FrontHarness h;
    REQUIRE(h.front.start());
    REQUIRE(h.front.add_route(h.embedded_route(kSession, kKey)));
    CHECK(h.front.add_route(h.embedded_route(kSession, session_key(0x50))).error().id == "front.route_exists");
    CHECK(h.front.add_route(h.embedded_route(session_id(2), kKey)).error().id == "front.key_in_use");

    h.front.remove_route(kSession);
    h.front.remove_route(kSession);
    CHECK(h.front.add_route(h.embedded_route(session_id(2), kKey)));
}

TEST_CASE("a plain-http upstream needs the lease's consent and is never a downgrade", "[front][session][tls]") {
    FrontHarness h;
    REQUIRE(h.front.start());
    CHECK(h.front.add_route(configured_route(kSession, kKey, origin("http://backend.example:8080"))).error().id ==
          "net.plain_http_needs_consent");
    CHECK(h.front.add_route(configured_route(kSession, kKey, origin("http://backend.example:8080"), std::nullopt, true)));

    h.tls.remember_https("secure.example");
    CHECK(h.front.add_route(configured_route(session_id(2), session_key(0x30), origin("http://secure.example"),
                                             std::nullopt, true))
              .error()
              .id == "net.https_downgrade_refused");
    CHECK(h.front.add_route(configured_route(session_id(3), session_key(0x40), origin("https://secure.example"))));
    CHECK(h.front.add_route(configured_route(session_id(4), session_key(0x60), origin("http://127.0.0.1:1"))));
}

TEST_CASE("the embedded backend gets the request without the prefix, with the session header", "[front][session]") {
    FrontHarness h;
    TestUpstream backend([](const Request&) { return respond(200, "hello"); });
    REQUIRE(h.front.start());
    h.front.set_embedded_backend(backend.port());
    REQUIRE(h.front.add_route(h.embedded_route(kSession, kKey)));

    Request request = get(prefix() + "/fortnite/api/version?x=1", h.port().value);
    request.set("X-Reboot-Session", "forged");
    request.set("x-reboot-console-key", "forged");
    request.set("X-Other", "kept");
    TestClient client;
    auto reply = client.send(h.port(), std::move(request));
    const Reply answer = h.await(reply);
    CHECK(answer.status == 200);
    CHECK(answer.body == "hello");

    const std::vector<Request> seen = backend.requests();
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].target() == "/fortnite/api/version?x=1");
    CHECK(seen[0][http::field::host] == "127.0.0.1:" + std::to_string(backend.port().value));
    CHECK(seen[0].count(kSessionHeader) == 1);
    CHECK(seen[0][kSessionHeader] == kKey.to_hex());
    CHECK(seen[0].count("X-Reboot-Console-Key") == 0);
    CHECK(seen[0]["X-Other"] == "kept");
}

TEST_CASE("the front answers for unknown keys, foreign hosts and origins, and a down backend", "[front][session]") {
    FrontHarness h;
    TestUpstream backend([](const Request&) { return respond(200); });
    REQUIRE(h.front.start());
    REQUIRE(h.front.add_route(h.embedded_route(kSession, kKey)));
    TestClient client;
    const unsigned short port = h.port().value;

    auto down = client.send(h.port(), get(prefix() + "/x", port));
    CHECK(h.await(down).status == static_cast<unsigned>(FrontAnswer::BackendDown));

    h.front.set_embedded_backend(backend.port());
    auto unknown = client.send(h.port(), get("/s/" + session_key(0x77).to_hex() + "/x", port));
    CHECK(h.await(unknown).status == static_cast<unsigned>(FrontAnswer::UnknownKey));
    auto unprefixed = client.send(h.port(), get("/fortnite/api/version", port));
    CHECK(h.await(unprefixed).status == 404);

    Request rebinding = get(prefix() + "/x", port);
    rebinding.set(http::field::host, "attacker.example:" + std::to_string(port));
    auto rebound = client.send(h.port(), std::move(rebinding));
    CHECK(h.await(rebound).status == static_cast<unsigned>(FrontAnswer::Forbidden));

    Request browser = get(prefix() + "/x", port);
    browser.set(http::field::origin, "https://attacker.example");
    auto foreign = client.send(h.port(), std::move(browser));
    CHECK(h.await(foreign).status == 403);

    Request websocket_origin = get(prefix() + "/x", port);
    websocket_origin.set(http::field::origin, "localhost");
    auto allowed = client.send(h.port(), std::move(websocket_origin));
    CHECK(h.await(allowed).status == 200);
    CHECK(backend.requests().size() == 1);
}

TEST_CASE("a backend that is restarting answers 503 rather than hanging", "[front][session]") {
    FrontHarness h;
    REQUIRE(h.front.start());
    std::optional<Port> closed;
    {
        TestUpstream gone([](const Request&) { return respond(200); });
        closed = gone.port();
    }
    h.front.set_embedded_backend(closed);
    REQUIRE(h.front.add_route(h.embedded_route(kSession, kKey)));
    TestClient client;
    auto reply = client.send(h.port(), get(prefix() + "/x", h.port().value));
    CHECK(h.await(reply).status == 503);
}

TEST_CASE("an upstream that never answers gets 504 at upstream_response", "[front][session][deadline]") {
    FrontHarness h;
    TestUpstream backend([](const Request&) { return UpstreamReply{UpstreamReply::Kind::Hang, {}, false}; });
    REQUIRE(h.front.start());
    h.front.set_embedded_backend(backend.port());
    REQUIRE(h.front.add_route(h.embedded_route(kSession, kKey)));
    TestClient client;
    auto reply = client.send(h.port(), get(prefix() + "/slow", h.port().value));
    REQUIRE(h.pump_until([&] { return backend.requests().size() == 1; }));
    CHECK(h.await_advancing(reply, 10s).status == static_cast<unsigned>(FrontAnswer::GatewayTimeout));
}

TEST_CASE("a client that never finishes its request head is closed", "[front][session][deadline]") {
    FrontHarness h;
    REQUIRE(h.front.start());
    REQUIRE(h.front.add_route(h.embedded_route(kSession, kKey)));
    TestClient client;
    auto reply = client.send_raw(h.port(), "GET " + prefix() + "/x HTTP/1.1\r\nHost: 127.0.0.1\r\n");
    CHECK(h.await_advancing(reply, 10s).empty());
}

TEST_CASE("keep-alive requests share one upstream connection", "[front][session]") {
    FrontHarness h;
    TestUpstream backend([](const Request& request) { return respond(200, std::string(request.target())); });
    REQUIRE(h.front.start());
    h.front.set_embedded_backend(backend.port());
    REQUIRE(h.front.add_route(h.embedded_route(kSession, kKey)));
    TestClient client;
    auto replies = client.send_all(h.port(), {get(prefix() + "/a", h.port().value), get(prefix() + "/b", h.port().value)});
    const std::vector<Reply> answers = h.await(replies);
    REQUIRE(answers.size() == 2);
    CHECK(answers[0].body == "/a");
    CHECK(answers[1].body == "/b");
    CHECK(backend.connections() == 1);
}

TEST_CASE("a kept-alive upstream connection that closed is replaced without failing the request", "[front][session]") {
    FrontHarness h;
    TestUpstream backend([](const Request& request) {
        UpstreamReply reply = respond(200, std::string(request.target()));
        reply.close_after = true;
        return reply;
    });
    REQUIRE(h.front.start());
    h.front.set_embedded_backend(backend.port());
    REQUIRE(h.front.add_route(h.embedded_route(kSession, kKey)));
    TestClient client;
    auto replies = client.send_all(h.port(), {get(prefix() + "/a", h.port().value), get(prefix() + "/b", h.port().value)});
    const std::vector<Reply> answers = h.await(replies);
    REQUIRE(answers.size() == 2);
    CHECK(answers[1].status == 200);
    CHECK(answers[1].body == "/b");
    CHECK(backend.connections() == 2);
}

TEST_CASE("Location headers keep the game on the session prefix", "[front][session]") {
    FrontHarness h;
    Port backend_port{};
    TestUpstream backend([&backend_port](const Request& request) {
        UpstreamReply reply = respond(302);
        if (request.target() == "/absolute")
            reply.response.set(http::field::location,
                               "http://127.0.0.1:" + std::to_string(backend_port.value) + "/next?x=1");
        else if (request.target() == "/relative")
            reply.response.set(http::field::location, "/next");
        else
            reply.response.set(http::field::location, "https://elsewhere.example/next");
        return reply;
    });
    backend_port = backend.port();
    REQUIRE(h.front.start());
    REQUIRE(h.front.add_route(configured_route(kSession, kKey, loopback_origin(backend.port()))));
    TestClient client;
    const std::string front_origin = "http://127.0.0.1:" + std::to_string(h.port().value);

    auto absolute = client.send(h.port(), get(prefix() + "/absolute", h.port().value));
    CHECK(header(h.await(absolute), "Location") == front_origin + prefix() + "/next?x=1");
    auto relative = client.send(h.port(), get(prefix() + "/relative", h.port().value));
    CHECK(header(h.await(relative), "Location") == prefix() + "/next");
    auto other = client.send(h.port(), get(prefix() + "/other", h.port().value));
    CHECK(header(h.await(other), "Location") == "https://elsewhere.example/next");
}

TEST_CASE("the ticket grant reaches the upstream with the stored login", "[front][session][ticket]") {
    FrontHarness h;
    TestUpstream backend([](const Request&) { return respond(200, R"({"access_token":"a"})", "application/json"); });
    REQUIRE(h.front.start());
    REQUIRE(h.front.add_route(configured_route(kSession, kKey, loopback_origin(backend.port()), TicketBinding::Bound)));
    TestClient client;

    auto first = client.send(h.port(), post_form(prefix() + std::string(kTokenPath), h.port().value, std::string(kTicketGrant)));
    CHECK(h.await(first).status == 200);
    auto other = client.send(h.port(), post_form(prefix() + std::string(kTokenPath), h.port().value,
                                                 "grant_type=exchange_code&exchange_code=abc"));
    CHECK(h.await(other).status == 200);
    // Bound tickets may log in again.
    auto again = client.send(h.port(), post_form(prefix() + std::string(kTokenPath), h.port().value, std::string(kTicketGrant)));
    CHECK(h.await(again).status == 200);

    const std::vector<Request> seen = backend.requests();
    REQUIRE(seen.size() == 3);
    CHECK(seen[0].body() == "grant_type=password&username=player%40example.com&password=p%26ss+w");
    CHECK(seen[0][http::field::content_length] == std::to_string(seen[0].body().size()));
    CHECK(seen[1].body() == "grant_type=exchange_code&exchange_code=abc");
    CHECK(seen[2].body() == seen[0].body());
}

TEST_CASE("an unbound ticket is spent by a successful login and refused after", "[front][session][ticket]") {
    FrontHarness h;
    int status = 401;
    TestUpstream backend([&status](const Request&) { return respond(static_cast<unsigned>(status), "{}", "application/json"); });
    REQUIRE(h.front.start());
    REQUIRE(h.front.add_route(configured_route(kSession, kKey, loopback_origin(backend.port()), TicketBinding::Unbound)));
    TestClient client;
    const auto login = [&] {
        return client.send(h.port(), post_form(prefix() + std::string(kTokenPath), h.port().value, std::string(kTicketGrant)));
    };

    auto rejected = login();
    CHECK(h.await(rejected).status == 401);
    status = 200;
    auto accepted = login();
    CHECK(h.await(accepted).status == 200);
    auto spent = login();
    const Reply refused = h.await(spent);
    CHECK(refused.status == kRefusedGrantStatus);
    CHECK(refused.body == kRefusedGrantBody);
    CHECK(header(refused, "X-Epic-Error-Code") == kRefusedGrantErrorCode);
    CHECK(header(refused, "X-Epic-Error-Name") == kRefusedGrantErrorName);
    CHECK(backend.requests().size() == 2);
}

TEST_CASE("where the peer uid is known, only the engine's user may redeem the ticket", "[front][session][ticket][peer]") {
    FrontOptions options;
    options.engine_uid = 1000;
    FrontHarness h(options, true);
    TestUpstream backend([](const Request&) { return respond(200, "{}", "application/json"); });
    REQUIRE(h.front.start());
    REQUIRE(h.front.add_route(configured_route(kSession, kKey, loopback_origin(backend.port()), TicketBinding::Bound)));
    TestClient client;
    const Endpoint front_end{IpAddress::v4(0x7F000001), h.port()};

    auto stranger = client.send(h.port(), post_form(prefix() + std::string(kTokenPath), h.port().value, std::string(kTicketGrant)),
                                [&](Endpoint local) { h.peers.set_peer_uid(front_end, local, 1001); });
    CHECK(h.await(stranger).status == kRefusedGrantStatus);
    CHECK(backend.requests().empty());

    auto engine_user =
        client.send(h.port(), post_form(prefix() + std::string(kTokenPath), h.port().value, std::string(kTicketGrant)),
                    [&](Endpoint local) { h.peers.set_peer_uid(front_end, local, 1000); });
    CHECK(h.await(engine_user).status == 200);
    REQUIRE(backend.requests().size() == 1);
    CHECK(backend.requests()[0].body().find("p%26ss+w") != std::string::npos);
}

TEST_CASE("a token grant on a relay never carries the stored login", "[front][session][ticket][relay]") {
    FrontHarness h;
    TestUpstream relayed([](const Request&) { return respond(200, "{}", "application/json"); });
    const std::string service = "ws://127.0.0.1:" + std::to_string(relayed.port().value);
    TestUpstream backend([&](const Request&) {
        return respond(200, R"({"serviceUrl":")" + service + R"("})", "application/json");
    });
    REQUIRE(h.front.start());
    REQUIRE(h.front.add_route(configured_route(kSession, kKey, loopback_origin(backend.port()), TicketBinding::Bound)));
    TestClient client;

    auto ticket = client.send(h.port(), get(prefix() + std::string(kTicketPath), h.port().value));
    CHECK(h.await(ticket).status == 200);
    const std::string relay = prefix() + "/@/ws/127.0.0.1:" + std::to_string(relayed.port().value);
    auto grant = client.send(h.port(), post_form(relay + std::string(kTokenPath), h.port().value, std::string(kTicketGrant)));
    CHECK(h.await(grant).status == 200);

    REQUIRE(relayed.requests().size() == 1);
    CHECK(relayed.requests()[0].target() == kTokenPath);
    CHECK(relayed.requests()[0].body() == kTicketGrant);
}

TEST_CASE("a learned serviceUrl opens its WebSocket relay", "[front][session][relay]") {
    FrontHarness h;
    TestUpstream matchmaker([](const Request&) { return UpstreamReply{UpstreamReply::Kind::SwitchAndEcho, {}, false}; });
    const std::string service = "ws://127.0.0.1:" + std::to_string(matchmaker.port().value);
    TestUpstream backend([&](const Request&) {
        const std::vector<u8> body = gzip(R"({"serviceUrl":")" + service + R"(","payload":"x"})");
        UpstreamReply reply = respond(200, std::string(body.begin(), body.end()), "application/json");
        reply.response.set(http::field::content_encoding, "gzip");
        return reply;
    });
    REQUIRE(h.front.start());
    REQUIRE(h.front.add_route(configured_route(kSession, kKey, loopback_origin(backend.port()))));
    TestClient client;
    const std::string relay = prefix() + "/@/ws/127.0.0.1:" + std::to_string(matchmaker.port().value) + "/";

    auto early = client.send(h.port(), get(relay, h.port().value));
    CHECK(h.await(early).status == static_cast<unsigned>(FrontAnswer::Forbidden));

    auto ticket = client.send(h.port(), get(prefix() + std::string(kTicketPath), h.port().value));
    const Reply answer = h.await(ticket);
    CHECK(answer.status == 200);
    CHECK(header(answer, "Content-Encoding") == "gzip");

    auto echo = client.upgrade_and_echo(h.port(), relay, "<stream:stream>");
    CHECK(h.await(echo) == "<stream:stream>");
    REQUIRE(matchmaker.requests().size() == 1);
    CHECK(matchmaker.requests()[0].target() == "/");
    CHECK(matchmaker.requests()[0][http::field::host] == "127.0.0.1:" + std::to_string(matchmaker.port().value));
    CHECK(matchmaker.requests()[0][http::field::sec_websocket_protocol] == "xmpp");
}

TEST_CASE("a learned plain-ws origin waits for consent", "[front][session][relay][consent]") {
    FrontHarness h;
    TestUpstream backend([](const Request&) {
        return respond(200, R"({"serviceUrl":"ws://mms.example.invalid:9000"})", "application/json");
    });
    REQUIRE(h.front.start());
    REQUIRE(h.front.add_route(configured_route(kSession, kKey, loopback_origin(backend.port()))));
    TestClient client;
    const std::string relay = prefix() + "/@/ws/mms.example.invalid:9000/";

    auto ticket = client.send(h.port(), get(prefix() + std::string(kTicketPath), h.port().value));
    CHECK(h.await(ticket).status == 200);
    const UserRequest* prompt = pending_prompt(h);
    REQUIRE(prompt);
    CHECK(prompt->session == kSession);
    CHECK(std::any_cast<UnencryptedUpstreamPrompt>(prompt->payload).origin == "http://mms.example.invalid:9000");
    const RequestId id = prompt->id;

    auto waiting = client.send(h.port(), get(relay, h.port().value));
    CHECK(h.await(waiting).status == static_cast<unsigned>(FrontAnswer::BadGateway));

    CHECK(h.runtime.requests().respond(id, std::string("yes")).error().id == "front.unexpected_answer");
    REQUIRE(h.runtime.requests().respond(id, UnencryptedUpstreamAnswer{false, false}));
    auto declined = client.send(h.port(), get(relay, h.port().value));
    CHECK(h.await(declined).status == static_cast<unsigned>(FrontAnswer::Forbidden));
    CHECK(!h.tls.check(net::UrlScheme::Http, "mms.example.invalid"));
}

TEST_CASE("an accepted and remembered plain-ws origin is acknowledged for good", "[front][session][relay][consent]") {
    FrontHarness h;
    TestUpstream backend([](const Request&) {
        return respond(200, R"({"serviceUrl":"ws://mms.example.invalid:9000"})", "application/json");
    });
    REQUIRE(h.front.start());
    REQUIRE(h.front.add_route(configured_route(kSession, kKey, loopback_origin(backend.port()))));
    TestClient client;
    auto ticket = client.send(h.port(), get(prefix() + std::string(kTicketPath), h.port().value));
    CHECK(h.await(ticket).status == 200);
    const UserRequest* prompt = pending_prompt(h);
    REQUIRE(prompt);
    REQUIRE(h.runtime.requests().respond(prompt->id, UnencryptedUpstreamAnswer{true, true}));
    CHECK(h.tls.check(net::UrlScheme::Http, "mms.example.invalid"));
}

TEST_CASE("removing a route withdraws its prompts and closes its exchanges", "[front][session][remove]") {
    FrontHarness h;
    TestUpstream backend([](const Request& request) {
        if (request.target() == "/hang") return UpstreamReply{UpstreamReply::Kind::Hang, {}, false};
        return respond(200, R"({"serviceUrl":"ws://mms.example.invalid:9000"})", "application/json");
    });
    REQUIRE(h.front.start());
    REQUIRE(h.front.add_route(configured_route(kSession, kKey, loopback_origin(backend.port()))));
    TestClient client;
    auto ticket = client.send(h.port(), get(prefix() + std::string(kTicketPath), h.port().value));
    CHECK(h.await(ticket).status == 200);
    REQUIRE(pending_prompt(h));

    auto hanging = client.send(h.port(), get(prefix() + "/hang", h.port().value));
    REQUIRE(h.pump_until([&] { return backend.requests().size() == 2; }));
    h.front.remove_route(kSession);
    CHECK(h.await(hanging).status == 0);
    h.pump();
    CHECK(!pending_prompt(h));

    auto gone = client.send(h.port(), get(prefix() + "/x", h.port().value));
    CHECK(h.await(gone).status == 404);
}

TEST_CASE("a pinned self-signed backend is reached over TLS, an unpinned one is refused", "[front][session][tls]") {
    FrontHarness h;
    const TestCertificate certificate = make_test_certificate();
    TestUpstream backend([](const Request&) { return respond(200, "secure"); }, certificate.server);
    REQUIRE(h.front.start());
    const UpstreamOrigin backend_origin{net::UrlScheme::Https, "127.0.0.1", backend.port()};
    REQUIRE(h.front.add_route(configured_route(kSession, kKey, backend_origin, std::nullopt, false, certificate.pin)));
    REQUIRE(h.front.add_route(configured_route(session_id(2), session_key(0x40), backend_origin)));
    std::array<u8, 32> wrong = certificate.pin;
    wrong[0] ^= 1;
    REQUIRE(h.front.add_route(configured_route(session_id(3), session_key(0x60), backend_origin, std::nullopt, false, wrong)));
    TestClient client;

    auto pinned = client.send(h.port(), get(prefix() + "/x", h.port().value));
    const Reply answer = h.await(pinned);
    CHECK(answer.status == 200);
    CHECK(answer.body == "secure");
    REQUIRE(backend.requests().size() == 1);
    CHECK(backend.requests()[0][http::field::host] == "127.0.0.1:" + std::to_string(backend.port().value));

    auto unpinned = client.send(h.port(), get("/s/" + session_key(0x40).to_hex() + "/x", h.port().value));
    CHECK(h.await(unpinned).status == static_cast<unsigned>(FrontAnswer::BadGateway));
    auto mismatched = client.send(h.port(), get("/s/" + session_key(0x60).to_hex() + "/x", h.port().value));
    CHECK(h.await(mismatched).status == 502);
    CHECK(backend.requests().size() == 1);
}

TEST_CASE("stop closes the listener and runs done once connections are gone", "[front][session][stop]") {
    FrontHarness h;
    TestUpstream backend([](const Request& request) {
        if (request.target() == "/hang") return UpstreamReply{UpstreamReply::Kind::Hang, {}, false};
        return respond(200);
    });
    REQUIRE(h.front.start());
    const Port port = h.port();
    h.front.set_embedded_backend(backend.port());
    REQUIRE(h.front.add_route(h.embedded_route(kSession, kKey)));
    TestClient client;
    auto hanging = client.send(port, get(prefix() + "/hang", port.value));
    REQUIRE(h.pump_until([&] { return backend.requests().size() == 1; }));

    bool done = false;
    h.front.stop(5s, [&] { done = true; });
    CHECK(!h.front.port());
    CHECK(h.front.origin(kKey).error().id == "front.not_started");
    h.pump();
    CHECK(!done);
    CHECK(h.await_advancing(hanging, 1s).status == 0);
    REQUIRE(h.pump_until([&] { return done; }));

    auto refused = client.send(port, get(prefix() + "/x", port.value));
    CHECK(h.await(refused).status == 0);
}

TEST_CASE("stop with no connections finishes without waiting for the grace", "[front][session][stop]") {
    FrontHarness h;
    REQUIRE(h.front.start());
    bool done = false;
    h.front.stop(1h, [&] { done = true; });
    CHECK(h.pump_until([&] { return done; }));
}
