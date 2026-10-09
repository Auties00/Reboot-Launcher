#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "download_support.hpp"
#include "gateway_codes.hpp"
#include "gateway_diagnostic.hpp"
#include "net_test_support.hpp"
#include "reboot/net/download_error.hpp"
#include "reboot/net/http_error.hpp"
#include "reboot/net/http_response.hpp"
#include "reboot/net/port_conflict.hpp"
#include "reboot/net/port_mapper_service.hpp"
#include "reboot/net/port_mapping_gateway.hpp"
#include "reboot/net/resolve_error.hpp"
#include "url.hpp"

using namespace rb;
using namespace rb::net;
using rb::net::test::arg_text;

TEST_CASE("parse_url accepts absolute http and https URLs only", "[net][url]") {
    const auto plain = parse_url("HTTP://Example.COM:8080/path?q=1#x");
    REQUIRE(plain);
    CHECK(plain->scheme == UrlScheme::Http);
    CHECK(plain->host == "example.com");
    CHECK(plain->port == Port{8080});

    const auto v6 = parse_url("https://[2001:DB8::1]/x");
    REQUIRE(v6);
    CHECK(v6->scheme == UrlScheme::Https);
    CHECK(v6->host == "2001:db8::1");
    CHECK_FALSE(v6->port);

    CHECK_FALSE(parse_url("ftp://example.com/"));
    CHECK_FALSE(parse_url("example.com/path"));
    CHECK_FALSE(parse_url("https://user:pass@example.com/"));
    CHECK_FALSE(parse_url("https://exa mple.com/"));
    CHECK_FALSE(parse_url("https://example.com:0/"));
    CHECK_FALSE(parse_url("https://example.com:/"));
    CHECK_FALSE(parse_url("https:///path"));
    CHECK_FALSE(parse_url("https://[1.2.3.4]/"));
    CHECK_FALSE(parse_url("https://[::1/"));
}

TEST_CASE("normalize_host drops brackets and a port and lowercases", "[net][url]") {
    CHECK(normalize_host("Example.com:443") == "example.com");
    CHECK(normalize_host("[::1]:80") == "::1");
    CHECK(normalize_host("fe80::1") == "fe80::1");
    CHECK(host_for_diagnostic("https://Host.test/x") == "host.test");
    CHECK(host_for_diagnostic("not a url") == "not a url");
}

TEST_CASE("find_header matches names case-insensitively and returns the first", "[net][http]") {
    const std::vector<ports::HttpHeader> headers{{"Content-Type", "a"}, {"content-type", "b"}};
    REQUIRE(find_header(headers, "CONTENT-TYPE") != nullptr);
    CHECK(*find_header(headers, "CONTENT-TYPE") == "a");
    CHECK(find_header(headers, "ETag") == nullptr);
}

TEST_CASE("HttpError diagnostics carry their ids, limits and retryability", "[net][errors]") {
    const Diagnostic stalled =
        to_diagnostic(HttpError{.code = HttpErrorCode::Stalled, .host = "h", .limit = std::chrono::seconds{30}});
    CHECK(stalled.id == "net.transfer_stalled");
    CHECK(stalled.retryable);
    CHECK(arg_text(stalled, "limit") == "30000ms");

    const Diagnostic tls = to_diagnostic(HttpError{.code = HttpErrorCode::Tls, .host = "h", .detail = "bad\ncert"});
    CHECK(tls.id == "net.tls_failed");
    CHECK_FALSE(tls.retryable);
    CHECK(tls.detail.has_value());

    const Diagnostic invalid = to_diagnostic(HttpError{.code = HttpErrorCode::InvalidUrl, .host = "nope"});
    CHECK(invalid.id == "net.invalid_url");
    CHECK(arg_text(invalid, "url") == "nope");
    CHECK(invalid.kind == ErrorKind::InvalidInput);

    const Diagnostic large = to_diagnostic(HttpError{.code = HttpErrorCode::ResponseTooLarge, .host = "h", .byte_limit = 10});
    CHECK(arg_text(large, "byte_limit") == "10");
    CHECK(to_diagnostic(HttpError{.code = HttpErrorCode::Cancelled, .host = "h"}).kind == ErrorKind::Cancelled);
    for (const HttpErrorCode code : {HttpErrorCode::Dns, HttpErrorCode::Connect, HttpErrorCode::ConnectTimeout,
                                     HttpErrorCode::TotalTimeout, HttpErrorCode::Transport})
        CHECK(to_diagnostic(HttpError{.code = code, .host = "h"}).retryable);
}

TEST_CASE("DownloadError and ResolveError diagnostics keep their causes", "[net][errors]") {
    DownloadError exhausted;
    exhausted.code = DownloadErrorCode::AttemptsExhausted;
    exhausted.host = "h";
    exhausted.attempts = 8;
    exhausted.cause = to_diagnostic(HttpError{.code = HttpErrorCode::Connect, .host = "h"});
    const Diagnostic diag = to_diagnostic(exhausted);
    CHECK(diag.id == "net.download_attempts_exhausted");
    CHECK(arg_text(diag, "attempts") == "8");
    REQUIRE(diag.causes.size() == 1);
    CHECK(diag.causes.front().id == "net.connect_failed");

    DownloadError space;
    space.code = DownloadErrorCode::InsufficientSpace;
    space.file = "a.zip";
    space.needed_bytes = 100;
    space.free_bytes = 5;
    const Diagnostic space_diag = to_diagnostic(space);
    CHECK(space_diag.id == "net.insufficient_space");
    CHECK(arg_text(space_diag, "needed") == "100");
    CHECK(arg_text(space_diag, "available") == "5");

    const Diagnostic invalid =
        to_diagnostic(ResolveError{.code = ResolveErrorCode::InvalidAddress, .address = "[x", .parse_error = AddressError::BracketMismatch});
    CHECK(invalid.id == "net.address_invalid");
    CHECK(invalid.kind == ErrorKind::InvalidInput);
    CHECK(to_diagnostic(ResolveError{.code = ResolveErrorCode::Timeout, .address = "a"}).id == "net.resolve_timeout");
    CHECK(to_diagnostic(ResolveError{.code = ResolveErrorCode::NoIpv4Address, .address = "a"}).id == "net.no_ipv4_address");
}

TEST_CASE("a PortConflict names the owner, the system or an unreadable owner", "[net][errors]") {
    PortConflict busy;
    busy.protocol = PortProtocol::Udp;
    busy.bind = Endpoint{IpAddress::v4(0), Port{7777}};
    PortOwnerInfo owner;
    owner.owner_class = PortOwnerClass::Ours;
    owner.owner.pid = 42;
    owner.owner.exe = NativePath("dir") / "reboot-game-server.exe";
    busy.owners.push_back(owner);
    const Diagnostic diag = to_diagnostic(busy);
    CHECK(diag.id == "net.port_busy");
    CHECK(arg_text(diag, "owner") == "reboot-game-server.exe");
    CHECK(arg_text(diag, "owned_by_us") == "true");
    CHECK(arg_text(diag, "protocol") == "UDP");
    CHECK(arg_text(diag, "port") == "7777");
    CHECK(diag.kind == ErrorKind::Conflict);

    busy.owners.front().owner_class = PortOwnerClass::System;
    CHECK(to_diagnostic(busy).id == "net.port_held_by_system");

    // Another user's process, which the inspector reports without a pid or exe.
    busy.owners.front() = PortOwnerInfo{};
    CHECK(to_diagnostic(busy).id == "net.port_owner_unknown");

    busy.owners.clear();
    busy.lookup_error = make_diag(ErrorDomain::Net, MessageId{"net.port_test_failed"}).build();
    const Diagnostic unknown = to_diagnostic(busy);
    CHECK(unknown.id == "net.port_owner_unknown");
    CHECK(unknown.causes.size() == 1);

    busy.kind = PortConflictKind::AccessDenied;
    CHECK(to_diagnostic(busy).id == "net.port_access_denied");
}

TEST_CASE("Content-Range, validators and the sidecar round-trip", "[net][download]") {
    const auto range = parse_content_range("bytes 100-199/1000");
    REQUIRE(range);
    CHECK(range->first == 100);
    CHECK(range->last == 199);
    CHECK(range->total == 1000u);
    CHECK_FALSE(parse_content_range("bytes 100-199/*")->total);
    CHECK_FALSE(parse_content_range("bytes 200-100/1000"));
    CHECK_FALSE(parse_content_range("bytes 100-199/150"));
    CHECK_FALSE(parse_content_range("items 0-1/2"));

    CHECK(response_validator({{"ETag", "\"abc\""}, {"Last-Modified", "Mon"}}) == "\"abc\"");
    CHECK(response_validator({{"ETag", "W/\"weak\""}, {"Last-Modified", "Mon"}}) == "Mon");
    CHECK(response_validator({}).empty());

    const ResumeSidecar sidecar{"https://h/a.zip", "\"v1\"", 1234};
    const auto decoded = decode_sidecar(encode_sidecar(sidecar));
    REQUIRE(decoded);
    CHECK(decoded->url == sidecar.url);
    CHECK(decoded->validator == sidecar.validator);
    CHECK(decoded->total == 1234u);
    const auto unknown_total = decode_sidecar(encode_sidecar(ResumeSidecar{"https://h/a", "v", std::nullopt}));
    REQUIRE(unknown_total);
    CHECK_FALSE(unknown_total->total);
    CHECK_FALSE(decode_sidecar(rb::net::test::bytes_of("garbage")));
    CHECK_FALSE(decode_sidecar(rb::net::test::bytes_of("reboot-resume 1\nu\n\n-\n")));
}

TEST_CASE("gateway codes map UPnP and NAT-PMP results", "[net][mapping]") {
    CHECK(upnp_error(718).code == GatewayErrorCode::ExternalPortTaken);
    CHECK(upnp_error(725).code == GatewayErrorCode::OnlyPermanentLease);
    CHECK(upnp_error(606).code == GatewayErrorCode::Refused);
    CHECK(upnp_error(-3).code == GatewayErrorCode::Timeout);
    CHECK(upnp_error(501).code == GatewayErrorCode::Failed);
    CHECK(upnp_error(501).protocol_code == 501);
    CHECK(natpmp_error(-7).code == GatewayErrorCode::NoGateway);
    CHECK(natpmp_error(-51).code == GatewayErrorCode::Refused);
    CHECK(natpmp_error(-14).code == GatewayErrorCode::Unsupported);

    CHECK(gateway_diagnostic(GatewayError{GatewayErrorCode::NoGateway}).id == "net.no_gateway");
    const Diagnostic refused = gateway_diagnostic(GatewayError{GatewayErrorCode::Refused, 606, "no"}, Port{7777});
    CHECK(refused.id == "net.mapping_refused");
    CHECK(arg_text(refused, "port") == "7777");
    CHECK(arg_text(refused, "protocol_code") == "606");
}

TEST_CASE("mapping descriptions carry the engine tag and session digits", "[net][mapping]") {
    const Result<Uuid> uuid = parse_uuid("0123abcd-0000-4000-8000-000000000000");
    REQUIRE(uuid);
    const std::string description = mapping_description("00112233aabbccdd", SessionId{*uuid});
    CHECK(description == "Reboot Launcher 00112233aabbccdd/0123abcd");
    CHECK(description.size() == 41);
}

TEST_CASE("the gateway adapters name their method; NAT-PMP cannot list", "[net][mapping]") {
    const std::unique_ptr<IPortMappingGateway> upnp = make_miniupnpc_gateway();
    const std::unique_ptr<IPortMappingGateway> natpmp = make_natpmp_gateway();
    CHECK(upnp->method() == MappingMethod::Upnp);
    CHECK(natpmp->method() == MappingMethod::NatPmp);
    const auto listed = natpmp->list(std::chrono::seconds{1});
    REQUIRE_FALSE(listed);
    CHECK(listed.error().code == GatewayErrorCode::Unsupported);
}
