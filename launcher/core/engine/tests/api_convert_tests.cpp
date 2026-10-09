#include <catch2/catch_test_macros.hpp>

#include <any>
#include <chrono>
#include <string>
#include <utility>

#include "api_convert.hpp"
#include "api_requests.hpp"
#include "messages.hpp"
#include "reboot/backend/account_rename_conflict.hpp"
#include "reboot/browser/join_prompts.hpp"
#include "reboot/builds/user_version.hpp"
#include "reboot/front/unencrypted_upstream_prompt.hpp"
#include "reboot/secrets/needs_secret.hpp"
#include "reboot/secrets/secret_error.hpp"

using namespace rb;
using namespace rb::engine;
using namespace std::chrono_literals;

namespace {

[[nodiscard]] Uuid uuid_of(u8 tag) {
    Uuid uuid{};
    uuid.bytes.fill(tag);
    uuid.bytes[6] = 0x40;
    return uuid;
}

[[nodiscard]] UserRequest request(UserRequestKind kind, std::any payload) {
    UserRequest out;
    out.id = RequestId{5};
    out.kind = kind;
    out.payload = std::move(payload);
    return out;
}

}  // namespace

TEST_CASE("api conversion: every domain event kind but ForegroundHint has an API kind that maps back") {
    for (u16 value = 0; value <= static_cast<u16>(EventKind::StorageModeChanged); ++value) {
        const auto kind = static_cast<EventKind>(value);
        const std::optional<api::EventKind> wire = convert::event_kind(kind);
        if (kind == EventKind::ForegroundHint) {
            CHECK_FALSE(wire);
            continue;
        }
        REQUIRE(wire);
        CHECK(convert::event_kind(*wire) == kind);
    }
    CHECK_FALSE(convert::event_kind(api::EventKind::Resync));
    CHECK_FALSE(convert::event_kind(static_cast<api::EventKind>(999)));
}

TEST_CASE("api conversion: a diagnostic keeps its id, args, kind and OS error") {
    const Diagnostic diag = make_diag(ErrorDomain::Engine, msg::kLockFailed)
                                .arg("path", "state/engine.lock")
                                .arg("count", u64{3})
                                .os(SystemError{SystemError::Origin::GuestWindows, 5})
                                .kind(ErrorKind::Conflict)
                                .retryable()
                                .build();
    const api::Diagnostic wire = convert::diagnostic(diag);
    CHECK(wire.id == "engine.lock_failed");
    REQUIRE(wire.args.size() == 2);
    CHECK(wire.args[0].kind == api::ArgKind::String);
    CHECK(wire.args[1].kind == api::ArgKind::Unsigned);
    CHECK(wire.args[1].value == "3");
    CHECK(wire.os_origin == api::OsErrorOrigin::GuestWindows);
    CHECK(wire.os_code == 5);
    CHECK(wire.kind == api::ErrorKind::Conflict);
    CHECK(wire.retryable);
}

TEST_CASE("api conversion: game versions from a client are parsed as strictly as the domain parses them") {
    REQUIRE(convert::game_version(api::GameVersion{12, 41, std::nullopt}, "version"));
    const Result<GameVersion> patched = convert::game_version(api::GameVersion{3, 5, 1}, "version");
    REQUIRE(patched);
    CHECK(patched->patch == 1);
    const Result<GameVersion> huge = convert::game_version(api::GameVersion{70000, 1, std::nullopt}, "version");
    REQUIRE_FALSE(huge);
    CHECK(huge.error().is(msg::kInvalidRequest));
    CHECK(convert::game_version(*patched) == api::GameVersion{3, 5, 1});
}

TEST_CASE("api conversion: a secret target needs the scope its kind names") {
    api::SecretTarget host;
    host.kind = api::SecretKind::HostJoinPassword;
    host.host_profile = api::HostProfileId{uuid_of(1)};
    const Result<secrets::SecretTarget> parsed = convert::secret_target(host);
    REQUIRE(parsed);
    CHECK(parsed->kind == secrets::SecretKind::HostJoinPassword);
    CHECK(convert::secret_target(*parsed) == host);

    api::SecretTarget backend;
    backend.kind = api::SecretKind::BackendPassword;
    backend.backend = api::BackendHost{"Backend.Example", 3551};
    const Result<secrets::SecretTarget> remote = convert::secret_target(backend);
    REQUIRE(remote);
    const api::SecretTarget back = convert::secret_target(*remote);
    REQUIRE(back.backend);
    CHECK(back.backend->host == "backend.example");
    CHECK(back.backend->port == 3551);

    api::SecretTarget mismatched;
    mismatched.kind = api::SecretKind::JoinPassword;
    mismatched.host_profile = api::HostProfileId{uuid_of(2)};
    const Result<secrets::SecretTarget> refused = convert::secret_target(mismatched);
    REQUIRE_FALSE(refused);
    CHECK(secrets::has_error(refused.error(), secrets::SecretError::InvalidScope));

    api::SecretTarget unspecified;
    unspecified.host_profile = api::HostProfileId{uuid_of(3)};
    REQUIRE_FALSE(convert::secret_target(unspecified));
}

TEST_CASE("api conversion: a host profile round-trips, listing and bans included") {
    api::HostProfile wire;
    wire.id = api::HostProfileId{uuid_of(4)};
    wire.revision = 7;
    wire.name = "Arena";
    wire.version = api::HostVersion{api::GameVersion{14, 40, std::nullopt}, 14550713};
    wire.port.pinned = 7790;
    wire.port_mapping = true;
    wire.listing = api::Listing::Listed;
    wire.server_name = "Arena server";
    wire.max_players = 50;
    wire.playlist = "Playlist_DefaultSolo";
    wire.start.auto_at_players = 10;
    wire.tick_rate = 30;
    wire.match_end = api::MatchEndPolicy{api::MatchEndAction::Shutdown, 20};
    wire.operator_cidrs = {"10.0.0.0/8"};
    api::Ban ban;
    ban.address = "192.168.1.7";
    ban.reason = "griefing";
    ban.created_unix_ms = 1000;
    ban.expires_unix_ms = 9000;
    wire.bans.push_back(ban);
    wire.update_policy = api::HostUpdatePolicy::Manual;

    const Result<host::HostProfile> profile = convert::host_profile(wire);
    REQUIRE(profile);
    CHECK(profile->listing == storage::HostListing::Listed);
    CHECK(std::get<host::PinnedPorts>(profile->port).first == Port{7790});
    CHECK(std::get<gameserver::AutoAtPlayers>(profile->match.start).players == 10);
    REQUIRE(profile->operators.bans.size() == 1);
    CHECK(profile->operators.bans[0].expires == convert::from_unix_ms(9000));

    api::HostProfile back = convert::host_profile(*profile, true);
    CHECK(back.has_password);
    back.has_password = false;
    CHECK(back == wire);

    api::HostProfile unlisted = wire;
    unlisted.listing = api::Listing::Unlisted;
    CHECK(convert::host_profile(unlisted)->listing == storage::HostListing::Unlisted);
    api::HostProfile bad_port = wire;
    bad_port.port.pinned = 70000;
    CHECK_FALSE(convert::host_profile(bad_port));
    api::HostProfile bad_cidr = wire;
    bad_cidr.operator_cidrs = {"not an address"};
    CHECK_FALSE(convert::host_profile(bad_cidr));
}

TEST_CASE("api conversion: background failures become dismissible notices keyed by their id") {
    logging::BackgroundFailure failure;
    failure.id = logging::BackgroundFailureId{42};
    failure.diag = make_diag(ErrorDomain::Engine, msg::kWriteFailed).arg("document", "settings").build();
    failure.session = SessionId{uuid_of(5)};
    const api::Notice notice = convert::background_notice(failure);
    CHECK(notice.key.kind == "background_failure:42");
    CHECK(notice.dismissible);
    CHECK(notice.title.id == "engine.write_failed");
    CHECK(convert::background_failure_of(notice.key) == logging::BackgroundFailureId{42});
    CHECK_FALSE(convert::background_failure_of(api::NoticeKey{"unlisted_live", std::nullopt}));
    CHECK_FALSE(convert::background_failure_of(api::NoticeKey{"background_failure:x", std::nullopt}));
}

TEST_CASE("api requests: an answer becomes the payload's own answer type, or is refused") {
    const UserRequest join = request(UserRequestKind::ConfirmJoin, browser::ConfirmJoinPrompt{});
    api::RequestAnswer accept;
    accept.decision = api::Decision{true, false};
    const Result<std::any> accepted = requests::answer_for(join, accept);
    REQUIRE(accepted);
    CHECK(std::any_cast<browser::ConfirmJoinAnswer>(*accepted).accept);

    api::RequestAnswer version;
    version.version = api::GameVersion{12, 41, std::nullopt};
    const Result<std::any> wrong = requests::answer_for(join, version);
    REQUIRE_FALSE(wrong);
    CHECK(wrong.error().is(msg::kAnswerMismatch));

    const UserRequest choose = request(UserRequestKind::ChooseVersion, std::any{});
    const Result<std::any> chosen = requests::answer_for(choose, version);
    REQUIRE(chosen);
    CHECK(std::any_cast<builds::UserVersion>(*chosen).version == GameVersion{12, 41, std::nullopt});

    const UserRequest rename = request(UserRequestKind::AccountRenameConflict, backend::AccountRenameConflictPrompt{});
    api::RequestAnswer ask;
    ask.rename = api::RenameConflict::Ask;
    CHECK_FALSE(requests::answer_for(rename, ask));
    api::RequestAnswer replace;
    replace.rename = api::RenameConflict::Replace;
    CHECK(std::any_cast<backend::AccountRenameConflictAnswer>(*requests::answer_for(rename, replace)).choice ==
          backend::RenameConflictChoice::Replace);

    const UserRequest secret = request(UserRequestKind::NeedsSecret, std::any{});
    api::RequestAnswer provided;
    provided.secret_provided = true;
    CHECK(requests::answer_for(secret, provided));
    api::RequestAnswer not_provided;
    not_provided.secret_provided = false;
    CHECK_FALSE(requests::answer_for(secret, not_provided));

    const UserRequest upstream =
        request(UserRequestKind::ConfirmUnencryptedUpstream, front::UnencryptedUpstreamPrompt{"http://a:80"});
    api::RequestAnswer remember;
    remember.decision = api::Decision{true, true};
    const auto answer = std::any_cast<front::UnencryptedUpstreamAnswer>(*requests::answer_for(upstream, remember));
    CHECK(answer.accept);
    CHECK(answer.remember);
}
