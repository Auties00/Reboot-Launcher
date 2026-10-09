#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "browser_test_support.hpp"
#include "reboot/browser/browse_choices.hpp"
#include "reboot/browser/connection_state.hpp"
#include "reboot/browser/deep_link.hpp"
#include "reboot/browser/full_jitter_backoff.hpp"
#include "reboot/browser/join_outcome.hpp"
#include "reboot/browser/list_state.hpp"
#include "reboot/browser/rbsb_endpoint.hpp"
#include "reboot/browser/rbsb_request_error.hpp"
#include "reboot/browser/server_row.hpp"
#include "reboot/browser/token_bucket.hpp"
#include "reboot/browser/view_spec.hpp"

using namespace rb;
using namespace rb::browser;
using namespace rb::browser::test;
using namespace std::chrono_literals;

TEST_CASE("connect failures are classified from the probes", "[browser][values]") {
    using P = HttpsProbe;
    const auto classify = [](bool dns, bool timed_out, HttpsProbe status, HttpsProbe connectivity) {
        return classify_connect_failure(ConnectFailureEvidence{dns, timed_out, status, connectivity});
    };
    CHECK(classify(true, false, P::NotRun, P::Ok) == ConnectionState::ServiceDown);
    CHECK(classify(true, false, P::NotRun, P::Unavailable) == ConnectionState::ServiceDown);
    CHECK(classify(true, false, P::NotRun, P::Failed) == ConnectionState::Offline);
    CHECK(classify(true, false, P::NotRun, P::NotRun) == ConnectionState::Backoff);
    CHECK(classify(false, true, P::Ok, P::NotRun) == ConnectionState::UdpBlocked);
    CHECK(classify(false, false, P::Ok, P::NotRun) == ConnectionState::Backoff);
    CHECK(classify(false, true, P::Unavailable, P::NotRun) == ConnectionState::ServiceDown);
    CHECK(classify(false, true, P::Failed, P::Ok) == ConnectionState::ServiceDown);
    CHECK(classify(false, true, P::Failed, P::Failed) == ConnectionState::Offline);
    CHECK(classify(false, false, P::NotRun, P::NotRun) == ConnectionState::Backoff);
}

TEST_CASE("deep links parse in any case, quoted, with a trailing slash or bare", "[browser][values]") {
    const ServerId id = server_id(9);
    const std::string uuid = format_uuid(id.value);
    std::string upper = uuid;
    for (char& c : upper)
        if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
    for (const std::string& text : std::vector<std::string>{"reboot://" + uuid, "Reboot://" + uuid + "/", "REBOOT://" + upper,
                                     "\"reboot://" + uuid + "\"", "  " + uuid + " ", "'" + uuid + "'"}) {
        INFO(text);
        const auto link = parse_deep_link(text);
        REQUIRE(link);
        CHECK(link->server == id);
    }
    for (const std::string& text : std::vector<std::string>{std::string(), "reboot://", "reboot://" + uuid + "/join", "https://" + uuid,
                                    "reboot:/" + uuid, "reboot://00000000-0000-0000-0000-000000000000",
                                    "reboot://" + uuid + "//", "\"reboot://" + uuid}) {
        INFO(text);
        const auto link = parse_deep_link(text);
        REQUIRE_FALSE(link);
        CHECK(link.error().id == "browser.invalid_link");
        CHECK(link.error().kind == ErrorKind::InvalidInput);
    }
}

TEST_CASE("full jitter stays under its doubling ceiling and the cap", "[browser][values]") {
    testing::FakeRandom random(3);
    FullJitterBackoff backoff(random);
    for (u32 attempt = 0; attempt < 12; ++attempt) {
        const auto ceiling = std::min(FullJitterBackoff::kCap, FullJitterBackoff::kBase * (std::chrono::milliseconds::rep{1} << std::min(attempt, 5u)));
        const auto delay = backoff.next();
        CHECK(delay >= 0ms);
        CHECK(delay <= ceiling);
    }
    CHECK(backoff.attempt() == 12);
    backoff.reset();
    CHECK(backoff.attempt() == 0);
    CHECK(backoff.next() <= FullJitterBackoff::kBase);

    for (int i = 0; i < 20; ++i) CHECK(go_away_delay(random, 500ms) <= 500ms);
    CHECK(go_away_delay(random, 0ms) == 0ms);
}

TEST_CASE("the token bucket allows its burst, then paces, and holds off", "[browser][values]") {
    ManualClock clock;
    TokenBucket bucket(clock, kJoinRate);
    for (int i = 0; i < 5; ++i) CHECK_FALSE(bucket.try_take());
    const auto wait = bucket.try_take();
    REQUIRE(wait);
    CHECK(*wait == 12s);
    clock.advance(12s);
    CHECK_FALSE(bucket.try_take());

    bucket.hold_off(30s);
    CHECK(bucket.try_take() == 30s);
    clock.advance(29s);
    CHECK(bucket.try_take() == 1s);
    clock.advance(1s);
    CHECK_FALSE(bucket.try_take());
    CHECK(bucket.try_take() == 12s);
}

TEST_CASE("the rbsb endpoint comes from the manifest, the expert override or the build", "[browser][values]") {
    const RbsbEndpoint compiled = select_rbsb_endpoint(std::nullopt, std::nullopt);
    CHECK(compiled.host == kCompiledRbsbHost);
    CHECK(compiled.port == Port{443});
    CHECK(compiled.source == EndpointSource::Compiled);
    CHECK(compiled.status_url() == "https://sb.rebootfn.org/status");

    const auto expert = RbsbExpertOverride::parse(" staging.test:4433 ", NativePath("ca.pem"));
    REQUIRE(expert);
    CHECK(expert->endpoint == HostPort{"staging.test", Port{4433}});
    CHECK(RbsbExpertOverride::parse("self.test", std::nullopt)->endpoint.port == Port{443});
    CHECK(RbsbExpertOverride::parse("bad:host:1", std::nullopt).error().id == "browser.invalid_endpoint_override");
    CHECK(RbsbExpertOverride::parse("", std::nullopt).error().id == "browser.invalid_endpoint_override");

    const RbsbEndpoint chosen = select_rbsb_endpoint(std::nullopt, *expert);
    CHECK(chosen.host == "staging.test");
    CHECK(chosen.port == Port{4433});
    CHECK(chosen.ca_bundle == NativePath("ca.pem"));
    CHECK(chosen.source == EndpointSource::Expert);

    const RbsbEndpoint manifest = select_rbsb_endpoint(components::EndpointOverride{"edge.example", Port{8443}}, *expert);
    CHECK(manifest.host == "edge.example");
    CHECK(manifest.source == EndpointSource::Manifest);
    CHECK_FALSE(manifest.ca_bundle);
    CHECK(RbsbEndpoint{"::1", Port{443}, std::nullopt, EndpointSource::Expert}.status_url() == "https://[::1]/status");
}

TEST_CASE("view specs validate their window and exact version", "[browser][values]") {
    ViewSpec spec;
    CHECK(spec.validate());
    spec.window = 0;
    CHECK(spec.validate().error().id == "browser.invalid_view_spec");
    spec.window = 50;
    spec.versions.exact_version = *GameVersion::parse("8.51");
    CHECK(spec.validate());
    spec.versions.buckets = {GameVersion::parse("9.10")->bucket()};
    CHECK(std::get<std::string>(*spec.validate().error().find_arg("field")) == "exact_version");
    spec.versions.buckets.push_back(GameVersion::parse("8.51")->bucket());
    CHECK(spec.validate());
    spec.versions.buckets.push_back(0);
    CHECK_FALSE(spec.validate());
}

TEST_CASE("browse choices become a view spec", "[browser][values]") {
    BrowseChoices choices;
    choices.password = PasswordFilter::Without;
    choices.region = Region::Asia;
    choices.sort = ServerSort::Newest;
    const std::vector<GameVersion> installed{*GameVersion::parse("9.10"), *GameVersion::parse("8.51"),
                                             *GameVersion::parse("8.51.1")};
    ViewSpec all = make_view_spec(choices, installed, kLargeWindow);
    CHECK(all.versions.buckets.empty());
    CHECK(all.password == PasswordFilter::Without);
    CHECK(all.region == Region::Asia);
    CHECK(all.sort == ServerSort::Newest);
    CHECK(all.window == kLargeWindow);

    choices.versions = VersionScope::Installed;
    const ViewSpec mine = make_view_spec(choices, installed, kSmallWindow);
    CHECK(mine.versions.buckets ==
          std::vector<u32>{GameVersion::parse("8.51")->bucket(), GameVersion::parse("9.10")->bucket()});
    CHECK(mine.validate());
    CHECK(make_view_spec(choices, {}, kSmallWindow).versions.buckets.empty());
}

TEST_CASE("list entries become sanitised rows on the local clock", "[browser][values]") {
    wire::ListEntry listed = entry(1, 1, "Bad\xE2\x80\xAE" "name", "8.51", 3,
                                   wire::entry_flag::has_password | wire::entry_flag::online | wire::entry_flag::hidden);
    listed.author = "x\x01y";
    listed.region = wire::Region::oceania;
    listed.created_ms = 10'000;
    const ServerRow row = make_server_row(listed, 4s);
    CHECK(row.id == server_id(1));
    CHECK(row.name == "Badname");
    CHECK(row.author == "xy");
    CHECK(row.players == 3);
    CHECK(row.region == Region::Oceania);
    CHECK(row.has_password);
    CHECK(row.online);
    CHECK(row.hidden);
    CHECK_FALSE(row.reachable);
    CHECK(row.created_at == std::chrono::system_clock::time_point(6s));

    listed.region = static_cast<wire::Region>(99);
    CHECK(make_server_row(listed, 0s).region == Region::All);
    const ServerDetails details = make_server_details(wire::EntryDetails{listed, "About", 20'000}, 4s);
    CHECK(details.description == "About");
    CHECK(details.updated_at == std::chrono::system_clock::time_point(16s));

    ViewUpdate update;
    const std::size_t empty = update.approx_bytes();
    update.rows.push_back(row);
    CHECK(update.approx_bytes() > empty + row.name.size());
}

TEST_CASE("request errors and join failures map to one message each", "[browser][values]") {
    const auto rejected = [](wire::ErrorCode code) {
        return to_diagnostic(RbsbRequestError{RbsbFailure::Rejected, code, "edge text", 2s, std::nullopt});
    };
    CHECK(rejected(wire::ErrorCode::bad_request).id == "browser.request_invalid");
    CHECK(rejected(wire::ErrorCode::unsupported).id == "browser.request_unsupported");
    CHECK(rejected(wire::ErrorCode::rate_limited).id == "browser.rate_limited");
    CHECK(rejected(wire::ErrorCode::not_found).id == "browser.server_not_found");
    CHECK(rejected(wire::ErrorCode::wrong_password).id == "browser.wrong_password");
    CHECK(rejected(wire::ErrorCode::unreachable).id == "browser.server_unreachable");
    CHECK(rejected(wire::ErrorCode::limit_exceeded).id == "browser.too_many_views");
    CHECK(rejected(wire::ErrorCode::unavailable).id == "browser.edge_unavailable");
    CHECK(rejected(wire::ErrorCode::internal).id == "browser.edge_internal_error");
    CHECK(rejected(wire::ErrorCode::internal).detail == "edge text");

    const Diagnostic cause = make_diag(ErrorDomain::Browser, MessageId{"browser.offline"}).build();
    CHECK(to_diagnostic(RbsbRequestError{RbsbFailure::NotConnected, {}, {}, {}, cause}).id == "browser.offline");
    CHECK(to_diagnostic(RbsbRequestError{RbsbFailure::NotConnected, {}, {}, {}, std::nullopt}).id == "browser.not_connected");
    CHECK(to_diagnostic(RbsbRequestError{RbsbFailure::ConnectionLost, {}, {}, {}, std::nullopt}).id ==
          "browser.connection_lost");
    CHECK(to_diagnostic(RbsbRequestError{RbsbFailure::TimedOut, {}, {}, {}, std::nullopt}).id == "browser.request_timeout");
    CHECK(to_diagnostic(RbsbRequestError{RbsbFailure::Cancelled, {}, {}, {}, std::nullopt}).kind == ErrorKind::Cancelled);

    const auto failure = [](JoinFailureCode code) { return to_diagnostic(JoinFailure{.code = code}); };
    CHECK(failure(JoinFailureCode::OwnServer).id == "browser.join_own_server");
    CHECK(failure(JoinFailureCode::NotFound).id == "browser.server_not_found");
    CHECK(failure(JoinFailureCode::Offline).id == "browser.server_offline");
    CHECK(failure(JoinFailureCode::Unreachable).id == "browser.server_unreachable");
    CHECK(failure(JoinFailureCode::WrongPassword).id == "browser.wrong_password");
    CHECK(failure(JoinFailureCode::Refused).id == "browser.join_refused");
    const Diagnostic edge = to_diagnostic(JoinFailure{.code = JoinFailureCode::EdgeUnavailable, .cause = cause});
    CHECK(edge.id == "browser.edge_unavailable");
    REQUIRE(edge.causes.size() == 1);
    CHECK(edge.causes[0].id == "browser.offline");
}
