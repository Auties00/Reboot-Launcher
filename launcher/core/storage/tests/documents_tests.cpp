#include <chrono>
#include <string>
#include <vector>

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/storage/accounts_document.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/library_document.hpp"
#include "reboot/storage/resume_document.hpp"
#include "reboot/storage/runtime_document.hpp"
#include "reboot/storage/state_document.hpp"
#include "reboot/testing/fake_platform_paths.hpp"

using namespace reboot;
using namespace reboot::storage;

namespace {

namespace json = boost::json;

static_assert(Document<AccountsDocument>);
static_assert(Document<LibraryDocument>);
static_assert(Document<ResumeDocument>);
static_assert(Document<RuntimeDocument>);
static_assert(Document<StateDocument>);

template <class D>
[[nodiscard]] D round_trip(const D& document) {
    std::vector<ValueIssue> issues;
    D read = D::read(document.write(), issues);
    CHECK(issues.empty());
    return read;
}

[[nodiscard]] Uuid uuid(const char* text) { return *parse_uuid(text); }

}  // namespace

TEST_CASE("onboarding is recorded per shell", "[storage][documents]") {
    StateDocument state;
    state.onboarding.push_back({.shell = *ShellName::parse("cli"), .completed = true, .completed_steps = {"welcome"}});
    state.onboarding.push_back({.shell = *ShellName::parse("winui"), .completed = false, .completed_steps = {}});
    const StateDocument read = round_trip(state);
    CHECK(read.onboarding == state.onboarding);

    json::object values = state.write();
    values["onboarding"].as_array().push_back(json::object{{"shell", "Not A Shell"}});
    std::vector<ValueIssue> issues;
    const StateDocument with_bad_entry = StateDocument::read(values, issues);
    CHECK(with_bad_entry.onboarding.size() == 2);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "onboarding[2]");
    CHECK(issues[0].reason.id == "storage.invalid_shell_name");
}

TEST_CASE("a library keeps a build's version and drops dangling selections", "[storage][documents]") {
    LibraryDocument library;
    library.builds.push_back(LibraryEntry{.id = BuildId{uuid("6f1c2a4e-1b7d-4a53-9a5e-2f0d8c3b7a10")},
                                          .name = "8.51",
                                          .root = testing::default_fake_root() / "builds" / "8.51",
                                          .version = GameVersion{8, 51}});
    library.client_selection = library.builds[0].id;
    library.host_selection = BuildId{uuid("00000000-0000-4000-8000-000000000001")};

    const LibraryDocument read = round_trip(library);
    REQUIRE(read.builds.size() == 1);
    CHECK(read.builds[0].version == library.builds[0].version);
    CHECK(read.client_selection == library.client_selection);
    CHECK_FALSE(read.host_selection);
}

TEST_CASE("runtime.json records the ports the engine and its children held", "[storage][documents]") {
    RuntimeDocument runtime;
    runtime.engine_pid = 4100;
    runtime.engine_ports = {{Port{3551}, EnginePortRole::LegacyFixed}, {Port{50211}, EnginePortRole::Front}};
    const auto created = std::chrono::system_clock::time_point{std::chrono::seconds{5}};
    runtime.children.push_back(RecordedProcess{
        .pid = 4200, .created = created, .role = ChildRole::GameServer, .ports = {Port{7777}, Port{7778}}});
    const RuntimeDocument read = round_trip(runtime);
    CHECK(read.engine_ports == runtime.engine_ports);
    CHECK(read.children == runtime.children);
}

TEST_CASE("resume.json stores the payload version as a SemVer", "[storage][documents]") {
    ResumeDocument resume;
    resume.payload_version = *SemVer::parse("1.4.0-beta.2");
    resume.reopen_clients = {contracts::ipc::ClientKind::WindowsGui};
    const ResumeDocument read = round_trip(resume);
    CHECK(read.payload_version == resume.payload_version);
    CHECK(read.reopen_clients == resume.reopen_clients);
}

TEST_CASE("an account record with a bad tag is dropped", "[storage][documents]") {
    AccountsDocument accounts;
    accounts.records.push_back(AccountRecord{.record_id = AccountRecordId{uuid("9a3e7c51-2d44-4c1b-8f0a-61b2d5e9c3f7")},
                                             .role = contracts::backend::AccountRole::Host,
                                             .display_name = "Host",
                                             .tag = "abc123"});
    CHECK(round_trip(accounts).records == accounts.records);

    accounts.records[0].tag = "ABC123";
    std::vector<ValueIssue> issues;
    const AccountsDocument read = AccountsDocument::read(accounts.write(), issues);
    CHECK(read.records.empty());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].reason.id == "storage.invalid_value");
}

TEST_CASE("members a newer engine added to a record are kept", "[storage][documents]") {
    LibraryDocument library;
    library.builds.push_back(LibraryEntry{.id = BuildId{uuid("6f1c2a4e-1b7d-4a53-9a5e-2f0d8c3b7a10")},
                                          .name = "8.51",
                                          .root = testing::default_fake_root() / "builds" / "8.51"});
    json::object library_json = library.write();
    library_json["builds"].as_array()[0].as_object()["future"] = 7;
    std::vector<ValueIssue> issues;
    const LibraryDocument read_library = LibraryDocument::read(library_json, issues);
    CHECK(issues.empty());
    REQUIRE(read_library.builds.size() == 1);
    CHECK(read_library.builds[0].unknown == json::object{{"future", 7}});
    CHECK(read_library.write() == library_json);

    AccountsDocument accounts;
    accounts.records.push_back(AccountRecord{.record_id = AccountRecordId{uuid("9a3e7c51-2d44-4c1b-8f0a-61b2d5e9c3f7")},
                                             .role = contracts::backend::AccountRole::Client,
                                             .display_name = "Player",
                                             .tag = "abc123"});
    json::object accounts_json = accounts.write();
    accounts_json["records"].as_array()[0].as_object()["future"] = "kept";
    const AccountsDocument read_accounts = AccountsDocument::read(accounts_json, issues);
    CHECK(issues.empty());
    CHECK(read_accounts.write() == accounts_json);
}

TEST_CASE("a recorded child with a pid no process can have is dropped", "[storage][documents]") {
    RuntimeDocument runtime;
    const auto created = std::chrono::system_clock::time_point{std::chrono::seconds{5}};
    runtime.children.push_back(RecordedProcess{.pid = 4200, .created = created, .role = ChildRole::Backend});
    runtime.children.push_back(RecordedProcess{.pid = 0, .created = created, .role = ChildRole::Game});
    runtime.children.push_back(RecordedProcess{.pid = 0xFFFFFFFFu, .created = created, .role = ChildRole::Game});
    std::vector<ValueIssue> issues;
    const RuntimeDocument read = RuntimeDocument::read(runtime.write(), issues);
    REQUIRE(read.children.size() == 1);
    CHECK(read.children[0].pid == 4200u);
    REQUIRE(issues.size() == 2);
    CHECK(issues[0].path == "children[1]");
    CHECK(issues[0].reason.id == "storage.out_of_range");
    CHECK(issues[1].path == "children[2]");
}
