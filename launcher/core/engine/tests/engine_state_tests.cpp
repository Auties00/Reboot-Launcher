#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>

#include "engine_state_json.hpp"
#include "engine_test_support.hpp"
#include "reboot/engine/engine_info.hpp"
#include "reboot/engine/engine_stores.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/storage/storage_mode_changed.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "runtime_recorder.hpp"
#include "state_extras.hpp"
#include "state_guidance_store.hpp"

using namespace reboot;
using namespace reboot::engine;
using namespace std::chrono_literals;

namespace {

[[nodiscard]] Uuid uuid_of(u8 tag) {
    Uuid uuid{};
    uuid.bytes.fill(tag);
    uuid.bytes[6] = 0x40;
    return uuid;
}

struct StoresFixture {
    StoresFixture() : strand(clock), fs(clock), paths(testing::default_fake_root()), layout(DataRoot{paths.default_data_root(), false}, paths) {
        for (const char* dir : {"config", "data", "state"}) fs.make_dir(paths.default_data_root() / dir);
        fs.make_dir(paths.default_cache_root());
        fs.make_dir(paths.default_data_root() / "data" / "prefixes");
    }
    ~StoresFixture() { workers.shutdown(); }

    void settle() {
        bool flushed = false;
        stores.flush_all([&flushed](Result<void> result) {
            REQUIRE(result);
            flushed = true;
        });
        strand.run_until([&flushed] { return flushed; });
    }

    ManualClock clock;
    test::TestStrand strand;
    WorkerPool workers{2};
    testing::InMemoryFileSystem fs;
    testing::FakePlatformPaths paths;
    AppLayout layout;
    EngineStores stores{fs, workers, strand, clock, layout};
};

}  // namespace

TEST_CASE("engine state: join targets round-trip through state.json") {
    const browser::JoinTarget server{browser::ServerTarget{ServerId{uuid_of(7)}, "Arena", "Host"}};
    const boost::json::value server_json = join_target_to_json(server);
    CHECK(join_target_from_json(&server_json) == server);
    const browser::JoinTarget address{browser::AddressTarget{"play.example:7778", HostPort{"play.example", Port{7778}}}};
    const boost::json::value written = join_target_to_json(address);
    CHECK(join_target_from_json(&written) == address);
    CHECK(join_target_to_json(std::nullopt).is_null());
    const boost::json::value broken = boost::json::parse(R"({"server":{"id":"not a uuid"}})");
    CHECK_FALSE(join_target_from_json(&broken));
    CHECK_FALSE(join_target_from_json(nullptr));
}

TEST_CASE("engine state: browse choices and serial floors round-trip, bad values read as defaults") {
    browser::BrowseChoices choices;
    choices.versions = browser::VersionScope::Installed;
    choices.password = browser::PasswordFilter::Without;
    choices.region = browser::Region::Europe;
    choices.sort = browser::ServerSort::Name;
    const boost::json::value written = browse_choices_to_json(choices);
    CHECK(browse_choices_from_json(&written) == choices);
    const boost::json::value odd = boost::json::parse(R"({"versions":"some","region":99,"sort":"name"})");
    const browser::BrowseChoices read = browse_choices_from_json(&odd);
    CHECK(read.versions == browser::VersionScope::All);
    CHECK(read.region == browser::Region::All);
    CHECK(read.sort == browser::ServerSort::Name);

    const boost::json::value floors = serials_to_json(SerialFloors{12, 34});
    CHECK(serials_from_json(&floors).catalog == 12);
    CHECK(serials_from_json(&floors).manifest == 34);
    CHECK(serials_from_json(nullptr).manifest == 0);
}

TEST_CASE("engine state: the port mapping marker keeps every entry it can read") {
    net::MappingRecord record;
    record.session = SessionId{uuid_of(3)};
    record.mapping.internal = Port{7777};
    record.mapping.external = Port{8777};
    record.mapping.method = net::MappingMethod::NatPmp;
    record.mapping.lease = 3600s;
    record.mapping.lan_address = IpAddress::v4(0xC0A80105);
    boost::json::value written = mapping_records_to_json({record});
    written.as_array().push_back(boost::json::parse(R"({"session":"x"})"));
    const std::vector<net::MappingRecord> read = mapping_records_from_json(&written);
    REQUIRE(read.size() == 1);
    CHECK(read[0].session == record.session);
    CHECK(read[0].mapping.internal == record.mapping.internal);
    CHECK(read[0].mapping.external == record.mapping.external);
    CHECK(read[0].mapping.method == net::MappingMethod::NatPmp);
    CHECK(read[0].mapping.lease == 3600s);
    CHECK(read[0].mapping.lan_address == record.mapping.lan_address);
}

TEST_CASE("engine stores: every document loads, and a data root that could not be made keeps them in memory") {
    StoresFixture f;
    const std::vector<storage::LoadReport> reports = f.stores.load(std::nullopt);
    CHECK(reports.size() == 10);
    CHECK(f.stores.mode() == storage::StorageMode::ReadWrite);

    StoresFixture memory;
    Diagnostic reason = make_diag(ErrorDomain::Storage, MessageId{"storage.root_unavailable"});
    const std::vector<storage::LoadReport> kept = memory.stores.load(reason);
    CHECK(memory.stores.mode() == storage::StorageMode::InMemory);
    for (const storage::LoadReport& report : kept) CHECK(report.mode == storage::StorageMode::InMemory);
}

TEST_CASE("engine stores: engine extras survive in state.json beside the typed members") {
    StoresFixture f;
    static_cast<void>(f.stores.load(std::nullopt));
    REQUIRE(put_state_extra(f.stores.state, kJoinTargetKey, boost::json::parse(R"({"address":{"host":"a","text":"a"}})")));
    REQUIRE(f.stores.state.update([](storage::StateDocument& document) { document.dismissed_notices.push_back("x"); }));
    f.settle();

    StoresFixture reread;
    reread.fs.write(f.layout.state_file(), *f.fs.contents(f.layout.state_file()));
    static_cast<void>(reread.stores.load(std::nullopt));
    const boost::json::value* target = state_extra(reread.stores.state.get(), kJoinTargetKey);
    REQUIRE(target != nullptr);
    CHECK(join_target_from_json(target)->target.index() == 1);
    REQUIRE(put_state_extra(reread.stores.state, kJoinTargetKey, nullptr));
    CHECK(state_extra(reread.stores.state.get(), kJoinTargetKey) == nullptr);
}

TEST_CASE("engine stores: a mode change reaches the bus once published") {
    StoresFixture f;
    static_cast<void>(f.stores.load(std::nullopt));
    EventBus bus(EngineEpoch{1});
    testing::EventRecorder recorder(bus, EventFilter{{EventKind::StorageModeChanged}, std::nullopt, std::nullopt});
    f.stores.publish_mode_changes(bus);
    f.fs.faults().fail_next(testing::FsOperation::AtomicReplace,
                            make_diag(ErrorDomain::Storage, MessageId{"storage.write_failed"}).build());
    REQUIRE(f.stores.settings.update([](storage::SettingsDocument& document) { document.values.ui.language = "de"; }));
    bool flushed = false;
    f.stores.settings.flush(CancelToken{}, [&flushed](Result<void>) { flushed = true; });
    f.strand.run_until([&flushed] { return flushed; });
    recorder.pump();
    const auto changes = recorder.payloads<storage::StorageModeChanged>(EventKind::StorageModeChanged);
    REQUIRE_FALSE(changes.empty());
    CHECK(changes.front()->document == "settings");
    CHECK(changes.front()->mode == storage::StorageMode::InMemory);
}

TEST_CASE("runtime recorder: children are recorded, forgotten when reaped, and the engine's start is written") {
    StoresFixture f;
    static_cast<void>(f.stores.load(std::nullopt));
    RuntimeRecorder recorder(f.stores.runtime);
    process::ChildRecord backend;
    backend.pid = 11;
    backend.created = std::chrono::system_clock::time_point{1s};
    backend.role = storage::ChildRole::Backend;
    process::ChildRecord server = backend;
    server.pid = 12;
    server.role = storage::ChildRole::GameServer;

    process::ChildRecordCallback record = recorder.recorder();
    record(backend, process::RecordChange::Spawned);
    record(server, process::RecordChange::Spawned);
    record(backend, process::RecordChange::Spawned);
    CHECK(recorder.recorded_children().size() == 2);
    record(server, process::RecordChange::Exited);
    REQUIRE(recorder.recorded_children().size() == 1);

    // A reused pid with another creation time is a different process.
    process::ChildRecord reused = backend;
    reused.created = std::chrono::system_clock::time_point{2s};
    REQUIRE(recorder.forget({reused}));
    CHECK(recorder.recorded_children().size() == 1);
    REQUIRE(recorder.forget({backend}));
    CHECK(recorder.recorded_children().empty());

    EngineInfo info;
    info.self.pid = 4242;
    info.endpoint = "pipe-name";
    info.origin = EngineOrigin::ServiceManager;
    REQUIRE(recorder.write_started(info));
    REQUIRE(recorder.set_ports({storage::EnginePort{Port{5000}, storage::EnginePortRole::Front}}));
    const storage::RuntimeDocument& document = f.stores.runtime.get();
    CHECK(document.engine_pid == 4242);
    CHECK(document.origin == EngineOrigin::ServiceManager);
    CHECK(document.engine_build == info.build);
    REQUIRE(document.engine_ports.size() == 1);
    CHECK(document.engine_ports[0].port == Port{5000});
}

TEST_CASE("state guidance store: the tour and one-time notices survive a reload") {
    StoresFixture f;
    static_cast<void>(f.stores.load(std::nullopt));
    StateGuidanceStore store(f.stores.state);
    CHECK(store.current().onboarding.status == ux::OnboardingStatus::Offered);

    ux::GuidanceState state;
    state.onboarding.status = ux::OnboardingStatus::InProgress;
    state.onboarding.current = ux::StepId::Library;
    state.onboarding.steps.push_back(ux::StepRecord{ux::StepId::Welcome, ux::StepState::Done, std::nullopt});
    state.onboarding.steps.push_back(
        ux::StepRecord{ux::StepId::HostListing, ux::StepState::Done, ux::OnboardingChoiceId::KeepUnlisted});
    ux::OneTimeNoticeRecord notice;
    notice.key = ux::NoticeKey{ux::NoticeKind::UnlistedLive, SessionId{uuid_of(9)}};
    notice.created_at = std::chrono::system_clock::time_point{5s};
    notice.args = {{"name", std::string("arena")}, {"players", u64{4}}};
    notice.dismissed = true;
    state.notices.push_back(notice);
    REQUIRE(store.replace(state));
    f.settle();

    StateGuidanceStore reread(f.stores.state);
    const ux::GuidanceState& read = reread.current();
    CHECK(read.onboarding.status == ux::OnboardingStatus::InProgress);
    CHECK(read.onboarding.current == ux::StepId::Library);
    REQUIRE(read.onboarding.steps.size() == 2);
    CHECK(read.onboarding.steps[1].chosen == ux::OnboardingChoiceId::KeepUnlisted);
    REQUIRE(read.notices.size() == 1);
    CHECK(read.notices[0].key == notice.key);
    CHECK(read.notices[0].dismissed);
    CHECK(read.notices[0].created_at == notice.created_at);
    REQUIRE(read.notices[0].args.size() == 2);
    CHECK(std::get<u64>(read.notices[0].args[1].second) == 4);
    CHECK(f.stores.state.get().dismissed_notices == std::vector<std::string>{"unlisted_live"});
}
