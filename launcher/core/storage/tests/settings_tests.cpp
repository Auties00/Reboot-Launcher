#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/json/object.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/settings.hpp"
#include "reboot/storage/settings_changed.hpp"
#include "reboot/storage/settings_document.hpp"
#include "reboot/storage/settings_keys.hpp"
#include "reboot/storage/settings_registry.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "test_support.hpp"

using namespace reboot;
using namespace reboot::storage;

namespace {

namespace json = boost::json;

// The key table is constant data, built before any code runs.
static_assert(keys::kHostListing.spec().id == "host.listing");
static_assert(keys::kHostListing.spec().reset_group == ResetGroup::Host);
static_assert(keys::kUiTheme.spec().kind == ValueKind::Choice);

struct Fixture {
    Fixture() {
        static_cast<void>(store.load_memory_only(make_diag(ErrorDomain::Storage, MessageId{"storage.memory_only"})));
    }

    test::WorkerStrand strand;
    WorkerPool workers{1};
    ManualClock clock;
    testing::InMemoryFileSystem fs;
    EventBus events{EngineEpoch{1}};
    testing::EventRecorder recorder{events, EventFilter{.kinds = {EventKind::SettingsChanged}}};
    DocumentStore<SettingsDocument> store{fs, workers, strand, clock, testing::default_fake_root() / "settings.json"};
    SettingsRegistry registry;
    Settings settings{store, registry, events};

    std::vector<const SettingsChanged*> changes() {
        recorder.pump();
        return recorder.payloads<SettingsChanged>(EventKind::SettingsChanged);
    }
};

[[nodiscard]] RemoteBackendAddress remote_address(std::string host) {
    return RemoteBackendAddress{.scheme = BackendScheme::Http, .endpoint = HostPort{std::move(host), std::nullopt}};
}

}  // namespace

TEST_CASE("every key label is a registered message", "[storage][settings]") {
    for (const AnyKey* key : keys::kAll) {
        const auto registered = std::ranges::any_of(
            message_registry(), [key](const MessageSpec* spec) { return spec->id == key->spec().label.id; });
        CHECK(registered);
    }
}

TEST_CASE("a patch publishes one SettingsChanged naming the changed keys", "[storage][settings]") {
    Fixture f;
    SettingsPatch patch;
    patch.ui.theme = Theme::Dark;
    patch.host.listing = HostListing::Listed;
    const Result<u64> revision = f.settings.patch(patch);
    REQUIRE(revision == 1u);

    const auto changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0]->revision == 1u);
    CHECK(changes[0]->keys == std::vector<std::string>{"host.listing", "ui.theme"});
    CHECK(f.settings.snapshot().values.host.listing == HostListing::Listed);

    // Nothing changed: same revision, no event.
    REQUIRE(f.settings.patch(patch) == 1u);
    CHECK(f.changes().size() == 1);
}

TEST_CASE("a patch with one bad field changes nothing", "[storage][settings]") {
    Fixture f;
    SettingsPatch patch;
    patch.ui.theme = Theme::Dark;
    patch.ui.language = "not a tag!";
    const Result<u64> refused = f.settings.patch(patch);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "storage.invalid_setting");
    REQUIRE(refused.error().causes.size() == 1);
    CHECK(refused.error().causes[0].id == "storage.invalid_language_tag");
    CHECK(f.settings.snapshot().values.ui.theme == Theme::System);
    CHECK(f.changes().empty());
}

TEST_CASE("an outdated expected revision is refused", "[storage][settings]") {
    Fixture f;
    SettingsPatch patch;
    patch.expected_revision = 7;
    patch.ui.theme = Theme::Dark;
    const Result<u64> refused = f.settings.patch(patch);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "storage.revision_conflict");
    CHECK(refused.error().kind == ErrorKind::Conflict);
}

TEST_CASE("keys are read and written by id", "[storage][settings]") {
    Fixture f;
    REQUIRE(f.settings.set("backend.console_key", json::string("Tilde"), std::nullopt));
    const Result<json::value> key = f.settings.get("backend.console_key");
    REQUIRE(key);
    CHECK(*key == json::value("Tilde"));

    const Result<u64> bad = f.settings.set("backend.console_key", json::string("tilde"), std::nullopt);
    REQUIRE_FALSE(bad);
    CHECK(bad.error().causes.at(0).id == "storage.invalid_console_key");
    const Result<json::value> unknown = f.settings.get("onboarding.completed");
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().id == "storage.unknown_key");
}

TEST_CASE("switching backend kinds keeps the remote address", "[storage][settings]") {
    Fixture f;
    SettingsPatch to_remote;
    to_remote.backend.target = BackendTarget{.kind = BackendKind::Remote, .remote = remote_address(" Example.ORG ")};
    REQUIRE(f.settings.patch(to_remote));

    BackendTarget embedded = f.settings.snapshot().values.backend.target;
    embedded.kind = BackendKind::Embedded;
    SettingsPatch to_embedded;
    to_embedded.backend.target = embedded;
    REQUIRE(f.settings.patch(to_embedded));

    const BackendTarget stored = f.settings.snapshot().values.backend.target;
    CHECK(stored.kind == BackendKind::Embedded);
    REQUIRE(stored.remote);
    CHECK(stored.remote->endpoint == HostPort{"example.org", Port{3551}});
    CHECK(stored.remote->scheme == BackendScheme::Http);
}

TEST_CASE("backend targets validate their addresses", "[storage][settings]") {
    const Result<BackendTarget> no_remote = BackendTarget::normalize(BackendTarget{.kind = BackendKind::Remote});
    REQUIRE_FALSE(no_remote);
    CHECK(no_remote.error().id == "storage.invalid_backend_target");

    const Result<BackendTarget> bad_host =
        BackendTarget::normalize(BackendTarget{.kind = BackendKind::Remote, .remote = remote_address("bad_host")});
    REQUIRE_FALSE(bad_host);
    CHECK(bad_host.error().id == "storage.invalid_host");

    BackendTarget local{.kind = BackendKind::Local};
    local.local.xmpp = HostPort{"[::1]", std::nullopt};
    const Result<BackendTarget> normalized = BackendTarget::normalize(local);
    REQUIRE(normalized);
    CHECK(normalized->local.xmpp == HostPort{"::1", kDefaultXmppPort});
}

TEST_CASE("a backend target round-trips through settings.json", "[storage][settings]") {
    BackendTarget target{.kind = BackendKind::Remote, .remote = remote_address("example.org")};
    target.remote->xmpp = HostPort{"xmpp.example.org", Port{5222}};
    const Result<BackendTarget> normalized = BackendTarget::normalize(target);
    REQUIRE(normalized);
    const Result<BackendTarget> decoded =
        SettingCodec<BackendTarget>::decode(SettingCodec<BackendTarget>::encode(*normalized));
    REQUIRE(decoded);
    CHECK(*decoded == *normalized);
}

TEST_CASE("reading settings.json replaces bad values and keeps unknown keys", "[storage][settings]") {
    json::object values;
    values["ui.theme"] = "purple";
    values["ui.language"] = "pt-BR";
    values["future.key"] = 3;
    std::vector<ValueIssue> issues;
    const SettingsDocument document = SettingsDocument::read(values, issues);

    CHECK(document.values.ui.theme == Theme::System);
    CHECK(document.values.ui.language == "pt-BR");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "ui.theme");
    CHECK(issues[0].reason.id == "storage.unknown_name");
    CHECK(document.write().at("future.key") == json::value(3));
}

TEST_CASE("reset_to_defaults is one revision and one event", "[storage][settings]") {
    Fixture f;
    SettingsPatch patch;
    patch.host.listing = HostListing::Listed;
    patch.host.update_policy = HostUpdatePolicy::Manual;
    REQUIRE(f.settings.patch(patch) == 1u);
    // Delivered now, or the bus would coalesce it with the reset's event.
    REQUIRE(f.changes().size() == 1);

    REQUIRE(f.settings.reset_to_defaults(f.registry.in_group(ResetGroup::Host)) == 2u);
    const auto changes = f.changes();
    REQUIRE(changes.size() == 2);
    CHECK(changes[1]->keys == std::vector<std::string>{"host.update_policy", "host.listing"});
    CHECK(f.settings.snapshot().values.host == HostSettings{});
}

TEST_CASE("play.env parses NAME=value lines and refuses anything else", "[storage][settings]") {
    const Result<ports::EnvBlock> parsed =
        parse_env_lines("DRI_PRIME=1\r\n\n__NV_PRIME_RENDER_OFFLOAD=1\nDXVK_HUD=\nDRI_PRIME=0");
    REQUIRE(parsed);
    using Vars = std::vector<std::pair<std::string, std::string>>;
    CHECK(parsed->vars ==
          Vars{{"DRI_PRIME", "1"}, {"__NV_PRIME_RENDER_OFFLOAD", "1"}, {"DXVK_HUD", ""}, {"DRI_PRIME", "0"}});
    CHECK(parse_env_lines("")->vars.empty());

    for (const std::string_view bad : {"DXVK_HUD", "=1", "1DRI=1", "DXVK HUD=1", "A=b\x01", "A=\xff"}) {
        const Result<ports::EnvBlock> refused = parse_env_lines(std::string("OK=1\n").append(bad));
        REQUIRE_FALSE(refused);
        CHECK(refused.error().id == "storage.invalid_env_line");
    }
}

TEST_CASE("the Wine settings patch and reset with the Play group", "[storage][settings]") {
    Fixture f;
    SettingsPatch patch;
    patch.play.env = "DXVK_HUD=fps";
    patch.play.verbose_wine_log = true;
    REQUIRE(f.settings.patch(patch) == 1u);
    CHECK(f.changes().at(0)->keys == std::vector<std::string>{"play.env", "play.verbose_wine_log"});
    CHECK(f.settings.snapshot().values.play.verbose_wine_log);

    const Result<u64> refused = f.settings.set("play.env", json::string("not a variable"), std::nullopt);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().causes.at(0).id == "storage.invalid_env_line");

    REQUIRE(f.settings.reset_to_defaults(f.registry.in_group(ResetGroup::Play)) == 2u);
    CHECK(f.settings.snapshot().values.play == PlaySettings{});
}

TEST_CASE("a hand edit publishes SettingsChanged for the keys it changed", "[storage][settings]") {
    test::WorkerStrand strand;
    WorkerPool workers{1};
    ManualClock clock;
    testing::InMemoryFileSystem fs;
    EventBus events{EngineEpoch{1}};
    testing::EventRecorder recorder{events, EventFilter{.kinds = {EventKind::SettingsChanged}}};
    const NativePath path = testing::default_fake_root() / "config" / "settings.json";
    fs.write_text(path, test::golden_text("hand_edit_before.json"));
    DocumentStore<SettingsDocument> store{fs, workers, strand, clock, path};
    REQUIRE(store.load().source == LoadSource::Primary);
    SettingsRegistry registry;
    Settings settings{store, registry, events};

    fs.write_text(path, test::golden_text("hand_edit_edited.json"));
    store.refresh();
    std::optional<Result<void>> flushed;
    store.flush({}, [&flushed](Result<void> done) { flushed = std::move(done); });
    strand.run_until([&flushed] { return flushed.has_value(); });
    REQUIRE(*flushed);

    recorder.pump();
    const auto changes = recorder.payloads<SettingsChanged>(EventKind::SettingsChanged);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0]->keys == std::vector<std::string>{"ui.language"});
    CHECK(changes[0]->revision == 5u);
    CHECK(settings.snapshot().values.ui.language == "de");
}

TEST_CASE("language tags, launch arguments and auth DLL paths are validated", "[storage][settings]") {
    for (const std::string_view tag : {"system", "de", "pt-BR", "zh-Hant-TW", "es-419"})
        CHECK(validate_language_tag(std::string(tag)));
    for (const std::string_view tag : {"", "e", "engl", "en_US", "en--US", "-en", "en-", "en-abcdefghi", "1en"}) {
        const Result<std::string> refused = validate_language_tag(std::string(tag));
        REQUIRE_FALSE(refused);
        CHECK(refused.error().id == "storage.invalid_language_tag");
    }

    for (const std::string_view args : {R"(-log "C:\Fortnite Builds\8.51" -nosplash)", R"(-msg "say \"hi\"")",
                                        "-a\t-b", R"(-dir C:\path\)"})
        CHECK(validate_launch_args(std::string(args)));
    for (const std::string_view args : {R"(-log "unterminated)", "-a\n-b", "-a\x7f", R"(-msg "say \\"hi")", "\xff"}) {
        const Result<std::string> refused = validate_launch_args(std::string(args));
        REQUIRE_FALSE(refused);
        CHECK(refused.error().id == "storage.invalid_launch_args");
    }

    CHECK(validate_auth_dll_path(std::nullopt));
    CHECK(validate_auth_dll_path(testing::default_fake_root() / "auth" / "Cobalt.DLL"));
    for (const NativePath& path : {NativePath("Cobalt.dll"), testing::default_fake_root() / "auth" / "cobalt.exe",
                                   testing::default_fake_root() / "auth"}) {
        const Result<std::optional<NativePath>> refused = validate_auth_dll_path(path);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().id == "storage.invalid_auth_dll_path");
    }
}
