// Each value must encode to the bytes protoc produced from tests/data/<name>.txtpb, and decode back.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

#include "reboot/api/codec.hpp"
#include "reboot/api/v1/events.hpp"
#include "reboot/api/v1/host.hpp"
#include "reboot/api/v1/method_table.hpp"
#include "reboot/api/v1/play.hpp"
#include "reboot/api/v1/requests.hpp"
#include "reboot/api/v1/secrets.hpp"
#include "reboot/api/v1/settings.hpp"

namespace api = reboot::api;

namespace {

api::Bytes golden(const std::string& name) {
    std::ifstream stream(std::string(REBOOT_API_TEST_DATA) + "/" + name + ".bin", std::ios::binary);
    REQUIRE(stream);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

reboot::Uuid ascending() {
    reboot::Uuid uuid;
    for (std::size_t i = 0; i < uuid.bytes.size(); ++i) uuid.bytes[i] = static_cast<reboot::u8>(i + 1);
    return uuid;
}

reboot::Uuid descending() {
    reboot::Uuid uuid;
    for (std::size_t i = 0; i < uuid.bytes.size(); ++i) uuid.bytes[i] = static_cast<reboot::u8>(0xFF - i);
    return uuid;
}

template <class T>
void check_golden(const std::string& name, const T& value) {
    const api::Bytes expected = golden(name);
    CHECK(api::encode(value) == expected);
    const auto decoded = api::decode<T>(expected);
    REQUIRE(decoded);
    CHECK(*decoded == value);
}

template <class T>
std::optional<api::DecodeError> decode_error(const api::Bytes& bytes) {
    const auto decoded = api::decode<T>(bytes);
    if (decoded) return std::nullopt;
    return decoded.error();
}

}  // namespace

TEST_CASE("Diagnostic matches protoc", "[golden]") {
    api::Diagnostic diag;
    diag.id = "play.wrong_session";
    diag.args = {{"session", api::ArgKind::String, "console"}, {"attempts", api::ArgKind::Signed, "-5"}};
    diag.detail = "literal text";
    diag.os_origin = api::OsErrorOrigin::GuestWindows;
    diag.os_code = -2147024891;
    diag.retryable = true;
    diag.kind = api::ErrorKind::Conflict;
    check_golden("diagnostic", diag);
}

TEST_CASE("a oneof member holding its zero value is still emitted", "[golden]") {
    api::Outcome cancelled;
    cancelled.op_id = 42;
    cancelled.method_id = api::kPlayStart;
    cancelled.cancelled = api::CancelReason::User;
    check_golden("outcome_cancelled", cancelled);

    api::Outcome completed;
    completed.op_id = 300;
    completed.method_id = api::kHostStart;
    completed.completed = api::Bytes{};
    check_golden("outcome_completed", completed);
}

TEST_CASE("EventFilter packs kinds and keeps a present zero", "[golden]") {
    api::EventFilter filter;
    filter.kinds = {api::EventKind::OpProgress, api::EventKind::SessionEnded, api::EventKind::LogLine};
    filter.session = api::SessionId{ascending()};
    filter.op_id = 0;
    check_golden("event_filter", filter);
}

TEST_CASE("Event envelope matches protoc", "[golden]") {
    api::Event event;
    event.kind = api::EventKind::LogLine;
    event.epoch = 3;
    event.seq = 1000;
    event.session = ascending();
    event.payload = {0xFA, 0x01, 0x00};
    check_golden("event", event);
}

TEST_CASE("PlayStartRequest matches protoc", "[golden]") {
    api::PlayStartRequest start;
    start.request.build = api::BuildId{ascending()};
    start.request.target = api::PlayTarget{.address = "127.0.0.1:7777"};
    start.request.auto_server = true;
    start.request.backend = api::BackendTarget{api::BackendKind::Remote, "https://backend.example"};
    start.request.custom_args = "-log \"-name=a b\"";
    start.request.environment = {{"WAYLAND_DISPLAY", "wayland-1"}, {"LANG", ""}};
    check_golden("play_start_request", start);
}

TEST_CASE("SecretTarget carries a typed scope", "[golden]") {
    api::SecretsStateRequest state;
    state.target.kind = api::SecretKind::BackendPassword;
    state.target.backend = api::BackendHost{"Backend.Example", 0};
    check_golden("secrets_state_request", state);
}

TEST_CASE("HostIdentityImportRequest matches protoc", "[golden]") {
    api::HostIdentityImportRequest request;
    request.profile = api::HostProfileId{ascending()};
    request.source = api::Path{"D:/exports/vps", api::Bytes{'D', ':', '/', 'e', 'x', 'p', 'o', 'r', 't', 's', '/', 'v', 'p', 's'}};
    check_golden("host_identity_import_request", request);
}

TEST_CASE("HostProfilesCreateRequest matches protoc", "[golden]") {
    api::HostProfilesCreateRequest create;
    api::HostProfile& profile = create.profile;
    profile.id = api::HostProfileId{descending()};
    profile.revision = 9;
    profile.name = "Default";
    profile.port.range = api::PortRange{7777, 7786};
    profile.port_mapping = true;
    profile.listing = api::Listing::Listed;
    profile.server_name = "Reboot";
    profile.description = "\xC3\x9Cn\xC3\xAF" "code";
    profile.max_players = 100;
    profile.playlist = "Playlist_DefaultSolo";
    profile.start.auto_at_players = 2;
    profile.tick_rate = 30;
    profile.match_end = api::MatchEndPolicy{api::MatchEndAction::Shutdown, 10};
    profile.operator_cidrs = {"10.0.0.0/8", "::1/128"};
    profile.bans = {api::Ban{"203.0.113.7", "cheater", "", 1700000000000}};
    check_golden("host_profiles_create_request", create);
}

TEST_CASE("SettingsPatchRequest matches protoc", "[golden]") {
    api::SettingsPatchRequest patch;
    patch.expected_revision = 12;
    patch.changes.push_back({"host.listing", {.text = "listed"}, false});
    patch.changes.push_back({"host.max_players", {.integer = -3}, true});
    patch.changes.push_back({"play.auto_server", {.flag = false}, false});
    patch.reset_keys = {"ui.language"};
    check_golden("settings_patch_request", patch);
}

TEST_CASE("EventPayload matches protoc", "[golden]") {
    api::LogLine line;
    line.entry.seq = 1;
    line.entry.unix_ms = 1700000000123;
    line.entry.level = api::LogLevel::Warn;
    line.entry.category = api::LogCategory::Host;
    line.entry.session = api::SessionId{ascending()};
    line.entry.text = "bound 7777";
    line.dropped = 2;
    api::EventPayload payload;
    payload.log_line = line;
    check_golden("event_payload", payload);
    CHECK(api::event_kind(payload) == api::EventKind::LogLine);
}

TEST_CASE("UserActionRequired matches protoc", "[golden]") {
    api::ServerEntry server;
    server.id = api::ServerId{descending()};
    server.name = "EU Solo";
    server.author = "host";
    server.version = "12.41";
    server.bucket = 12329;
    server.players = 3;
    server.max_players = 100;
    server.region = api::Region::Europe;
    server.has_password = true;
    server.reachable = true;
    server.online = true;
    server.created_unix_ms = 1700000000000;
    api::UserActionRequired request;
    request.request_id = 7;
    request.kind = api::UserRequestKind::ConfirmJoin;
    request.op_id = 5;
    request.prompt.confirm_join = api::ConfirmJoinPrompt{server};
    check_golden("user_action_required", request);
}

TEST_CASE("decode refuses a oneof with two members set", "[golden]") {
    api::Outcome outcome;
    outcome.completed = api::Bytes{1};
    outcome.timed_out = "install.downloading";
    CHECK(decode_error<api::Outcome>(api::encode(outcome)) == api::DecodeError::ConflictingCases);

    api::PlayStartRequest start;
    start.request.target = api::PlayTarget{.server = api::ServerId{ascending()}, .address = "127.0.0.1:7777"};
    CHECK(decode_error<api::PlayStartRequest>(api::encode(start)) == api::DecodeError::ConflictingCases);
}

TEST_CASE("a oneof with no member set is an unknown case", "[golden]") {
    // What an older build sees when a newer engine sets an alternative added after it.
    api::Outcome outcome;
    outcome.op_id = 1;
    CHECK(decode_error<api::Outcome>(api::encode(outcome)) == api::DecodeError::UnknownCase);

    api::PlayStartRequest start;
    start.request.target = api::PlayTarget{};
    CHECK(decode_error<api::PlayStartRequest>(api::encode(start)) == api::DecodeError::UnknownCase);

    api::PlayStartRequest join_target;
    CHECK(api::decode<api::PlayStartRequest>(api::encode(join_target)));
}

TEST_CASE("undecodable bytes are malformed", "[golden]") {
    const api::Bytes truncated{0x0A, 0x05, 0x01};
    CHECK(decode_error<api::Outcome>(truncated) == api::DecodeError::Malformed);
}

TEST_CASE("only kinds with an EventPayload member report one", "[golden]") {
    CHECK(api::has_payload(api::EventKind::LogLine));
    CHECK(api::has_payload(api::EventKind::UserActionResolved));
    CHECK_FALSE(api::has_payload(api::EventKind::Resync));
    CHECK_FALSE(api::has_payload(api::EventKind::Unspecified));
    CHECK_FALSE(api::has_payload(static_cast<api::EventKind>(1000)));
}
