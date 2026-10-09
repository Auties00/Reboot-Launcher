#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/support/support_policy.hpp"
#include "reboot/support/version_cap.hpp"

using namespace reboot;
using namespace reboot::support;

namespace {

using components::ManifestOs;
using ports::RunnerKind;

constexpr GameVersion version(u16 major, u16 minor, std::optional<u16> patch = std::nullopt) {
    return {major, minor, patch};
}

components::Sha256Digest digest(u8 fill) {
    components::Sha256Digest out{};
    out.fill(fill);
    return out;
}

VersionRange range(GameVersion min, GameVersion max) { return {.min = min, .max = max, .changelists = {}}; }

const VersionRange kSeason8 = range(version(8, 0), version(8, 51));

PlayCellInputs current_play_inputs() {
    return {.client_dll_sha256 = digest(1), .backend_content = {.schema = 1, .serial = 7}, .runner_pin = ""};
}

SupportInputs windows_inputs(std::vector<EvidenceRecord> evidence = {}) {
    return {.os = ManifestOs::Windows,
            .client_dll_sha256 = digest(1),
            .backend_content = {.schema = 1, .serial = 7},
            .runners = {{.runner = RunnerKind::Native, .runtime_id = ""}},
            .evidence = std::move(evidence)};
}

EvidenceRecord play_record(EvidenceResult result, i64 seconds, CellInputs inputs = current_play_inputs(),
                           ManifestOs os = ManifestOs::Windows) {
    return {.cell = {.range = kSeason8, .role = SupportRole::Play, .runner = RunnerKind::Native},
            .inputs = std::move(inputs),
            .os = os,
            .build = "8.51",
            .version = version(8, 51),
            .cl = std::nullopt,
            .recorded_at = std::chrono::system_clock::time_point(std::chrono::seconds(seconds)),
            .result = result,
            .log_ref = "run.log"};
}

HostInputs server(u8 sha_fill = 9) {
    return {.pin = {.game_server_sha256 = digest(sha_fill)}, .ranges = {range(version(3, 5), version(10, 40))}};
}

EvidenceRecord host_record(EvidenceResult result, i64 seconds, u8 sha_fill = 9) {
    return {.cell = {.range = range(version(3, 5), version(10, 40)), .role = SupportRole::Host,
                     .runner = RunnerKind::Native},
            .inputs = HostCellInputs{.game_server_sha256 = digest(sha_fill)},
            .os = ManifestOs::Windows,
            .build = "8.51",
            .version = version(8, 51),
            .cl = std::nullopt,
            .recorded_at = std::chrono::system_clock::time_point(std::chrono::seconds(seconds)),
            .result = result,
            .log_ref = "host.log"};
}

SupportQuery play_query(GameVersion v) {
    SupportQuery query;
    query.version = v;
    return query;
}

SupportQuery host_query(GameVersion v, std::optional<HostInputs> binary = server()) {
    SupportQuery query;
    query.version = v;
    query.role = SupportRole::Host;
    query.server = std::move(binary);
    return query;
}

bool has_reason(const SupportVerdict& verdict, SupportReason reason) {
    return std::ranges::find(verdict.reasons, reason) != verdict.reasons.end();
}

}  // namespace

TEST_CASE("the cap compares major.minor only: 30.10.x is allowed, 30.11 and above are not") {
    STATIC_CHECK_FALSE(above_version_cap(version(30, 10)));
    STATIC_CHECK_FALSE(above_version_cap(version(30, 10, 5)));
    STATIC_CHECK_FALSE(above_version_cap(version(29, 40)));
    STATIC_CHECK(above_version_cap(version(30, 11)));
    STATIC_CHECK(above_version_cap(version(31, 0)));
}

TEST_CASE("builds at the cap are not blocked by it") {
    const SupportPolicy policy(windows_inputs());
    for (const GameVersion v : {version(30, 10), version(30, 10, 2)}) {
        const SupportVerdict verdict = policy.evaluate(play_query(v));
        CHECK(verdict.tier == SupportTier::Untested);
        CHECK_FALSE(has_reason(verdict, SupportReason::AboveVersionCap));
    }
}

TEST_CASE("a changelist never moves a build under the cap") {
    const SupportPolicy policy(windows_inputs());
    for (const GameVersion v : {version(30, 11), version(31, 0)}) {
        SupportQuery query = play_query(v);
        query.cl = Changelist{1};
        const SupportVerdict verdict = policy.evaluate(query);
        CHECK(verdict.tier == SupportTier::Blocked);
        CHECK(verdict.reasons.front() == SupportReason::AboveVersionCap);
    }
}

TEST_CASE("the above-cap opt-in applies only to imported builds") {
    const SupportPolicy policy(windows_inputs());
    SupportQuery query = play_query(version(31, 0));

    query.above_cap_opt_in = true;
    SupportVerdict verdict = policy.evaluate(query);
    CHECK(verdict.tier == SupportTier::Blocked);
    CHECK_FALSE(verdict.opt_in_available);

    query.above_cap_opt_in = false;
    query.imported = true;
    verdict = policy.evaluate(query);
    CHECK(verdict.tier == SupportTier::Blocked);
    CHECK(verdict.opt_in_available);

    query.above_cap_opt_in = true;
    verdict = policy.evaluate(query);
    CHECK(verdict.tier == SupportTier::Untested);
    CHECK(verdict.reasons.front() == SupportReason::AboveVersionCapOptedIn);
    CHECK_FALSE(verdict.opt_in_available);
}

TEST_CASE("the opt-in lifts only the cap: an unpinned runner stays Blocked") {
    SupportInputs inputs = windows_inputs();
    inputs.os = ManifestOs::MacOs;
    inputs.runners.clear();
    const SupportPolicy policy(std::move(inputs));

    SupportQuery query = play_query(version(31, 0));
    query.runner = RunnerKind::MacRuntime;
    query.imported = true;
    query.above_cap_opt_in = true;
    const SupportVerdict verdict = policy.evaluate(query);
    CHECK(verdict.tier == SupportTier::Blocked);
    CHECK(verdict.reasons.front() == SupportReason::RunnerUnavailable);
    CHECK(has_reason(verdict, SupportReason::AboveVersionCapOptedIn));
}

TEST_CASE("the opt-in does not let a game server host a version it does not cover") {
    const SupportPolicy policy(windows_inputs());
    SupportQuery query = host_query(version(31, 0));
    query.imported = true;
    query.above_cap_opt_in = true;
    const SupportVerdict verdict = policy.evaluate(query);
    CHECK(verdict.tier == SupportTier::Blocked);
    CHECK(verdict.reasons.front() == SupportReason::NotCoveredByGameServer);
}

TEST_CASE("an unknown version is Blocked and falls in no cell") {
    const SupportPolicy policy(windows_inputs({play_record(EvidenceResult::Pass, 10)}));
    const SupportVerdict verdict = policy.evaluate(SupportQuery{});
    CHECK(verdict.tier == SupportTier::Blocked);
    CHECK(verdict.reasons == std::vector{SupportReason::VersionUnknown});
    CHECK_FALSE(verdict.cell);
}

TEST_CASE("the newest record for the current inputs decides the cell") {
    SECTION("an older failure followed by a pass is Tested") {
        const SupportPolicy policy(
            windows_inputs({play_record(EvidenceResult::Fail, 10), play_record(EvidenceResult::Pass, 20)}));
        const SupportVerdict verdict = policy.evaluate(play_query(version(8, 51)));
        CHECK(verdict.tier == SupportTier::Tested);
        CHECK(verdict.reasons.empty());
        REQUIRE(verdict.cell);
        REQUIRE(verdict.cell->evidence);
        CHECK(verdict.cell->evidence->recorded_at == std::chrono::system_clock::time_point(std::chrono::seconds(20)));
    }
    SECTION("an older pass followed by a failure is EvidenceFailed") {
        const SupportPolicy policy(
            windows_inputs({play_record(EvidenceResult::Fail, 20), play_record(EvidenceResult::Pass, 10)}));
        const SupportVerdict verdict = policy.evaluate(play_query(version(8, 51)));
        CHECK(verdict.tier == SupportTier::Untested);
        CHECK(verdict.reasons == std::vector{SupportReason::EvidenceFailed});
    }
    SECTION("a failure logged at the same time as a pass wins") {
        const SupportPolicy policy(
            windows_inputs({play_record(EvidenceResult::Pass, 10), play_record(EvidenceResult::Fail, 10)}));
        CHECK(policy.evaluate(play_query(version(8, 51))).reasons == std::vector{SupportReason::EvidenceFailed});
    }
}

TEST_CASE("NoEvidence, EvidenceStale and EvidenceFailed are told apart") {
    PlayCellInputs old_dll = current_play_inputs();
    old_dll.client_dll_sha256 = digest(2);
    PlayCellInputs old_content = current_play_inputs();
    old_content.backend_content.serial = 6;

    SECTION("no record") {
        const SupportPolicy policy(windows_inputs());
        CHECK(policy.evaluate(play_query(version(8, 51))).reasons == std::vector{SupportReason::NoEvidence});
    }
    SECTION("records only for other inputs") {
        const SupportPolicy policy(windows_inputs(
            {play_record(EvidenceResult::Pass, 30, old_dll), play_record(EvidenceResult::Pass, 40, old_content)}));
        CHECK(policy.evaluate(play_query(version(8, 51))).reasons == std::vector{SupportReason::EvidenceStale});
    }
    SECTION("a newer stale pass does not hide a failure for the current inputs") {
        const SupportPolicy policy(
            windows_inputs({play_record(EvidenceResult::Fail, 10), play_record(EvidenceResult::Pass, 30, old_dll)}));
        CHECK(policy.evaluate(play_query(version(8, 51))).reasons == std::vector{SupportReason::EvidenceFailed});
    }
    SECTION("records of another OS do not count") {
        const SupportPolicy policy(windows_inputs(
            {play_record(EvidenceResult::Pass, 10, current_play_inputs(), ManifestOs::Linux)}));
        CHECK(policy.evaluate(play_query(version(8, 51))).reasons == std::vector{SupportReason::NoEvidence});
    }
}

TEST_CASE("a custom auth DLL or an external backend caps a Tested cell at Untested") {
    const SupportPolicy policy(windows_inputs({play_record(EvidenceResult::Pass, 10)}));

    SupportQuery custom = play_query(version(8, 51));
    custom.custom_auth_dll = true;
    SupportVerdict verdict = policy.evaluate(custom);
    CHECK(verdict.tier == SupportTier::Untested);
    CHECK(verdict.provider == SupportProvider::Custom);
    CHECK(verdict.reasons == std::vector{SupportReason::CustomAuthDll});

    SupportQuery external = play_query(version(8, 51));
    external.embedded_backend = false;
    verdict = policy.evaluate(external);
    CHECK(verdict.tier == SupportTier::Untested);
    CHECK(verdict.provider == SupportProvider::Ours);
    CHECK(verdict.reasons == std::vector{SupportReason::ExternalBackend});
}

TEST_CASE("host cells are keyed to the queried game-server binary") {
    const SupportPolicy policy(windows_inputs({host_record(EvidenceResult::Pass, 10)}));

    CHECK(policy.evaluate(host_query(version(8, 51))).tier == SupportTier::Tested);
    CHECK(policy.evaluate(host_query(version(8, 51), server(8))).reasons ==
          std::vector{SupportReason::EvidenceStale});

    const SupportVerdict undescribed = policy.evaluate(host_query(version(8, 51), std::nullopt));
    CHECK(undescribed.tier == SupportTier::Blocked);
    CHECK(undescribed.reasons.front() == SupportReason::GameServerUnavailable);

    SupportQuery wine = host_query(version(8, 51));
    wine.runner = RunnerKind::Wine;
    const SupportVerdict on_wine = policy.evaluate(wine);
    CHECK(on_wine.tier == SupportTier::Blocked);
    CHECK(on_wine.reasons.front() == SupportReason::RunnerUnavailable);
    CHECK(to_diagnostic(on_wine.reasons.front(), wine).is(msg::kHostNeedsNativeRunner));
}

TEST_CASE("play with the auto server is never rated above the host cell") {
    SupportQuery play = play_query(version(8, 51));
    play.server = server();

    SECTION("Tested play, Untested host") {
        const SupportPolicy policy(windows_inputs({play_record(EvidenceResult::Pass, 10)}));
        const AutoServerVerdict verdict = policy.evaluate_with_auto_server(play);
        CHECK(verdict.play.tier == SupportTier::Tested);
        CHECK(verdict.host.tier == SupportTier::Untested);
        CHECK(verdict.tier == SupportTier::Untested);
    }
    SECTION("both Tested") {
        const SupportPolicy policy(
            windows_inputs({play_record(EvidenceResult::Pass, 10), host_record(EvidenceResult::Pass, 10)}));
        CHECK(policy.evaluate_with_auto_server(play).tier == SupportTier::Tested);
    }
    SECTION("an undescribed server blocks") {
        const SupportPolicy policy(windows_inputs({play_record(EvidenceResult::Pass, 10)}));
        play.server.reset();
        CHECK(policy.evaluate_with_auto_server(play).tier == SupportTier::Blocked);
    }
}

TEST_CASE("set_inputs updates the policy in place") {
    SupportPolicy policy(windows_inputs());
    const SupportPolicy& held = policy;
    CHECK(held.evaluate(play_query(version(8, 51))).tier == SupportTier::Untested);
    policy.set_inputs(windows_inputs({play_record(EvidenceResult::Pass, 10)}));
    CHECK(held.evaluate(play_query(version(8, 51))).tier == SupportTier::Tested);
}

TEST_CASE("cells lists every evidence cell of this OS and every described range") {
    EvidenceRecord above_cap = play_record(EvidenceResult::Pass, 10);
    above_cap.cell.range = range(version(31, 0), version(31, 10));
    const SupportPolicy policy(windows_inputs({play_record(EvidenceResult::Pass, 10), above_cap,
                                               play_record(EvidenceResult::Pass, 10, current_play_inputs(),
                                                           ManifestOs::Linux)}));

    const std::vector<SupportCell> cells = policy.cells(server());
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].tier == SupportTier::Tested);
    CHECK(cells[1].tier == SupportTier::Blocked);
    CHECK(cells[1].reasons.front() == SupportReason::AboveVersionCap);
    CHECK(cells[2].key.role == SupportRole::Host);
    CHECK(cells[2].reasons == std::vector{SupportReason::NoEvidence});
}

TEST_CASE("check_not_blocked fails only for Blocked verdicts") {
    const SupportPolicy policy(windows_inputs());

    SupportQuery imported = play_query(version(31, 0));
    imported.imported = true;
    imported.runner = RunnerKind::Umu;
    const Result<void> blocked = check_not_blocked(imported, policy.evaluate(imported));
    REQUIRE_FALSE(blocked);
    CHECK(blocked.error().domain == ErrorDomain::Support);
    CHECK(blocked.error().kind == ErrorKind::Unsupported);
    CHECK(blocked.error().is(msg::kAboveVersionCapOptInRequired));
    REQUIRE(blocked.error().causes.size() == 1);

    const SupportQuery untested = play_query(version(8, 51));
    CHECK(check_not_blocked(untested, policy.evaluate(untested)));
}

TEST_CASE("every reason converts to its own support diagnostic") {
    SupportQuery query = play_query(version(8, 51));
    std::vector<std::string> ids;
    for (u8 i = 0; i <= static_cast<u8>(SupportReason::EvidenceFailed); ++i) {
        const auto reason = static_cast<SupportReason>(i);
        const Diagnostic diag = to_diagnostic(reason, query);
        CHECK(diag.domain == ErrorDomain::Support);
        CHECK(diag.severity == (reason_tier(reason) == SupportTier::Blocked ? Severity::Error : Severity::Warning));
        CHECK(std::ranges::find(ids, diag.id) == ids.end());
        ids.push_back(diag.id);
    }
    CHECK(to_diagnostic(SupportReason::CustomAuthDll, query).is(msg::kCustomAuthDll));
    CHECK(to_diagnostic(SupportReason::NoEvidence, query).is(msg::kNoEvidence));
    CHECK(to_diagnostic(SupportReason::EvidenceStale, query).is(msg::kEvidenceStale));
    CHECK(to_diagnostic(SupportReason::EvidenceFailed, query).is(msg::kEvidenceFailed));
    CHECK(to_diagnostic(SupportReason::ExternalBackend, query).is(msg::kExternalBackend));
    CHECK(to_diagnostic(SupportReason::AboveVersionCapOptedIn, query).is(msg::kAboveVersionCapOptedIn));
}

TEST_CASE("an opted-in build in an above-cap cell keeps the cell's evidence") {
    EvidenceRecord record = play_record(EvidenceResult::Pass, 10);
    record.cell.range = range(version(31, 0), version(31, 10));
    record.version = version(31, 0);
    const SupportPolicy policy(windows_inputs({record}));

    SupportQuery query = play_query(version(31, 0));
    query.imported = true;
    query.above_cap_opt_in = true;
    const SupportVerdict verdict = policy.evaluate(query);
    CHECK(verdict.tier == SupportTier::Untested);
    CHECK(verdict.reasons == std::vector{SupportReason::AboveVersionCapOptedIn});
    REQUIRE(verdict.cell);
    CHECK(verdict.cell->evidence);

    query.above_cap_opt_in = false;
    CHECK(policy.evaluate(query).reasons == std::vector{SupportReason::AboveVersionCap});
}

TEST_CASE("an opted-in build is rated by its best cell, not by the cell's own cap reason") {
    // A wide cell with only stale evidence, then an above-cap cell with a pass.
    PlayCellInputs old_dll = current_play_inputs();
    old_dll.client_dll_sha256 = digest(2);
    EvidenceRecord wide = play_record(EvidenceResult::Pass, 10, old_dll);
    wide.cell.range = range(version(30, 0), version(31, 10));
    wide.version = version(31, 0);
    EvidenceRecord above = play_record(EvidenceResult::Pass, 10);
    above.cell.range = range(version(31, 0), version(31, 10));
    above.version = version(31, 0);
    const SupportPolicy policy(windows_inputs({wide, above}));

    SupportQuery query = play_query(version(31, 0));
    query.imported = true;
    query.above_cap_opt_in = true;
    const SupportVerdict verdict = policy.evaluate(query);
    CHECK(verdict.reasons == std::vector{SupportReason::AboveVersionCapOptedIn});
    REQUIRE(verdict.cell);
    CHECK(verdict.cell->key.range == above.cell.range);
    CHECK(verdict.cell->evidence);
}

TEST_CASE("a host query on a Wine runner falls in no Native cell") {
    const SupportPolicy policy(windows_inputs({host_record(EvidenceResult::Pass, 10)}));
    SupportQuery wine = host_query(version(8, 51));
    wine.runner = RunnerKind::Wine;
    const SupportVerdict verdict = policy.evaluate(wine);
    CHECK(verdict.tier == SupportTier::Blocked);
    CHECK_FALSE(verdict.cell);
}

TEST_CASE("the auto server's host side runs natively with our own DLL and backend") {
    SupportInputs inputs = windows_inputs({host_record(EvidenceResult::Pass, 10)});
    inputs.os = ManifestOs::Linux;
    inputs.runners = {{.runner = RunnerKind::Umu, .runtime_id = "ge-proton-10"}};
    inputs.evidence.front().os = ManifestOs::Linux;
    const SupportPolicy policy(std::move(inputs));

    SupportQuery play = play_query(version(8, 51));
    play.runner = RunnerKind::Umu;
    play.custom_auth_dll = true;
    play.embedded_backend = false;
    play.server = server();
    const AutoServerVerdict verdict = policy.evaluate_with_auto_server(play);
    CHECK(verdict.host.tier == SupportTier::Tested);
    CHECK(verdict.host.provider == SupportProvider::Ours);
    CHECK(verdict.play.tier == SupportTier::Untested);
    CHECK(verdict.tier == SupportTier::Untested);
}

TEST_CASE("cells without a described server block the host evidence cells") {
    const SupportPolicy policy(windows_inputs({host_record(EvidenceResult::Pass, 10)}));
    const std::vector<SupportCell> cells = policy.cells(std::nullopt);
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].tier == SupportTier::Blocked);
    CHECK(cells[0].reasons == std::vector{SupportReason::GameServerUnavailable});
    CHECK_FALSE(cells[0].evidence);
}

TEST_CASE("check_not_blocked lists the other Blocked reasons as causes") {
    SupportInputs inputs = windows_inputs();
    inputs.runners.clear();
    const SupportPolicy policy(std::move(inputs));
    SupportQuery query = play_query(version(31, 0));
    query.custom_auth_dll = true;
    const Result<void> blocked = check_not_blocked(query, policy.evaluate(query));
    REQUIRE_FALSE(blocked);
    CHECK(blocked.error().is(msg::kAboveVersionCap));
    REQUIRE(blocked.error().causes.size() == 1);
    CHECK(blocked.error().causes[0].is(msg::kRunnerUnavailable));
}
