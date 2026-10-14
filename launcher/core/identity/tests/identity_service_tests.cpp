#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/identity/display_name.hpp"
#include "reboot/identity/identity_changed_event.hpp"
#include "reboot/identity/identity_service.hpp"
#include "reboot/storage/accounts_document.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "test_strand.hpp"

using namespace rb;
using namespace rb::identity;
using storage::AccountsDocument;
using storage::BackendKind;
namespace json = boost::json;

namespace {

const AccountRecord kAlice{AccountRecordId{*parse_uuid("9a3e7c51-2d44-4c1b-8f0a-61b2d5e9c3f7")}, AccountRole::Client,
                           "Alice", "a1b2c3"};
const AccountRecord kServer{AccountRecordId{*parse_uuid("1c0b8a6e-5f3d-4e2a-9b7c-0d1e2f3a4b5c")}, AccountRole::Host,
                            "Alice", "z9y8x7"};

[[nodiscard]] std::string envelope(u32 schema, u64 revision, json::object values) {
    json::object out;
    out.emplace("schema", schema);
    out.emplace("revision", revision);
    out.emplace("values", std::move(values));
    return json::serialize(out);
}

[[nodiscard]] std::string envelope(u64 revision, json::object values) {
    return envelope(AccountsDocument::kSchema, revision, std::move(values));
}

// The document holding `records`, named after the first one.
[[nodiscard]] json::object records_json(std::vector<AccountRecord> records) {
    AccountsDocument document;
    if (!records.empty()) document.display_name = records.front().display_name;
    for (const AccountRecord& record : records)
        document.records.push_back(storage::StoredAccount{record.record_id, record.role, record.tag, {}});
    return document.write();
}

[[nodiscard]] AccountRecord named(AccountRecord record, std::string name) {
    record.display_name = std::move(name);
    return record;
}

[[nodiscard]] IdentitySnapshot named(const IdentitySnapshot& identity, const std::string& name) {
    return IdentitySnapshot{named(identity.client, name), named(identity.host, name)};
}

[[nodiscard]] HostPort endpoint(std::string host, u16 port = 3551) { return HostPort{std::move(host), Port{port}}; }

struct Fixture {
    Fixture() { fs.make_dir(data_dir); }

    void start() {
        static_cast<void>(accounts.load());
        static_cast<void>(logins.load());
        service.emplace(accounts, logins, random, events);
        service->ensure_records();
    }

    template <class D>
    void flush(storage::DocumentStore<D>& store) {
        std::optional<Result<void>> result;
        store.flush({}, [&result](Result<void> flushed) { result = std::move(flushed); });
        strand.run_until([&result] { return result.has_value(); });
        REQUIRE(*result);
    }

    // A hand edit of data/accounts.json, then the reload a file watcher would trigger.
    void hand_edit_accounts(json::object values) {
        fs.write_text(accounts_path, envelope(accounts.revision() + 1, std::move(values)));
        accounts.refresh();
        flush(accounts);
    }
    void hand_edit_accounts(std::vector<AccountRecord> records) { hand_edit_accounts(records_json(std::move(records))); }

    [[nodiscard]] AccountsDocument accounts_on_disk() const {
        const std::optional<std::string> text = fs.text(accounts_path);
        REQUIRE(text);
        std::vector<storage::ValueIssue> issues;
        AccountsDocument document = AccountsDocument::read(json::parse(*text).at("values").as_object(), issues);
        CHECK(issues.empty());
        return document;
    }

    // The stored records, each with the stored name.
    [[nodiscard]] std::vector<AccountRecord> records_on_disk() const {
        const AccountsDocument document = accounts_on_disk();
        std::vector<AccountRecord> out;
        for (const storage::StoredAccount& stored : document.records)
            out.push_back(AccountRecord{stored.record_id, stored.role, document.display_name, stored.tag});
        return out;
    }

    [[nodiscard]] std::vector<IdentityChangedEvent> changes() {
        recorder.pump();
        std::vector<IdentityChangedEvent> out;
        for (const IdentityChangedEvent* change : recorder.payloads<IdentityChangedEvent>(EventKind::IdentityChanged))
            out.push_back(*change);
        recorder.clear();
        return out;
    }

    test::TestStrand strand;
    ManualClock clock;
    testing::InMemoryFileSystem fs;
    // Declared after what its jobs use, so it joins them before those go away.
    WorkerPool workers{1};
    testing::FakeRandom random{42};
    EventBus events{EngineEpoch{1}};
    testing::EventRecorder recorder{events, EventFilter{.kinds = {EventKind::IdentityChanged}}};
    NativePath data_dir = testing::default_fake_root() / "data";
    NativePath accounts_path = data_dir / "accounts.json";
    NativePath logins_path = data_dir / "backend-logins.json";
    storage::DocumentStore<AccountsDocument> accounts{fs, workers, strand, clock, accounts_path};
    storage::DocumentStore<BackendLoginsDocument> logins{fs, workers, strand, clock, logins_path};
    std::optional<IdentityService> service;
};

}  // namespace

TEST_CASE("ensure_records mints a client and a host record under one default name and writes them",
          "[identity][service]") {
    Fixture f;
    f.start();
    const IdentitySnapshot& identity = f.service->get();
    CHECK(identity.client.role == AccountRole::Client);
    CHECK(identity.host.role == AccountRole::Host);
    CHECK(is_default_display_name(identity.display_name()));
    CHECK(identity.host.display_name == identity.display_name());
    CHECK(identity.client.tag.size() == kTagLength);
    CHECK(identity.host.tag.size() == kTagLength);
    CHECK(identity.client.record_id != identity.host.record_id);
    CHECK(account_id(identity.client) != account_id(identity.host));
    CHECK(f.service->record(AccountRole::Host) == identity.host);

    f.flush(f.accounts);
    CHECK(f.records_on_disk() == std::vector{identity.client, identity.host});
    // Startup sets the identity; there is no earlier one to change.
    CHECK(f.changes().empty());
}

TEST_CASE("stored records are kept and not written again", "[identity][service]") {
    Fixture f;
    const std::string stored = envelope(3, records_json({kServer, kAlice}));
    f.fs.write_text(f.accounts_path, stored);
    f.start();
    CHECK(f.service->get() == IdentitySnapshot{kAlice, kServer});
    CHECK(f.accounts.revision() == 3u);
    f.flush(f.accounts);
    CHECK(f.fs.text(f.accounts_path) == stored);
}

TEST_CASE("a schema 1 file, which named each role, keeps the client's name", "[identity][service]") {
    Fixture f;
    json::array records;
    for (const AccountRecord& record : {kAlice, named(kServer, "Server1")}) {
        json::object entry = records_json({record}).at("records").as_array()[0].as_object();
        entry["display_name"] = record.display_name;
        records.emplace_back(std::move(entry));
    }
    f.fs.write_text(f.accounts_path, envelope(1, 3, json::object{{"records", std::move(records)}}));
    f.start();
    CHECK(f.service->get() == IdentitySnapshot{kAlice, kServer});
    f.flush(f.accounts);
    CHECK(f.records_on_disk() == std::vector{kAlice, kServer});
}

TEST_CASE("a stored record with a bad tag keeps its record_id and gets a fresh tag", "[identity][service]") {
    Fixture f;
    json::object values = records_json({kAlice, kServer});
    values.at("records").as_array()[1].as_object()["tag"] = "NOT-OK";
    f.fs.write_text(f.accounts_path, envelope(3, std::move(values)));
    f.start();

    const IdentitySnapshot& identity = f.service->get();
    CHECK(identity.client == kAlice);
    CHECK(identity.host.record_id == kServer.record_id);
    CHECK(identity.host.display_name == kServer.display_name);
    CHECK(identity.host.tag.size() == kTagLength);
    CHECK(identity.host.tag != "NOT-OK");
    f.flush(f.accounts);
    CHECK(f.records_on_disk() == std::vector{kAlice, identity.host});
}

TEST_CASE("a bad stored name gets a default one; the records keep their ids and tags", "[identity][service]") {
    Fixture f;
    json::object values = records_json({kAlice, kServer});
    values["display_name"] = "Not Valid";
    f.fs.write_text(f.accounts_path, envelope(3, std::move(values)));
    f.start();

    const IdentitySnapshot& identity = f.service->get();
    CHECK(is_default_display_name(identity.display_name()));
    CHECK(identity == named(IdentitySnapshot{kAlice, kServer}, identity.display_name()));
    f.flush(f.accounts);
    CHECK(f.records_on_disk() == std::vector{identity.client, identity.host});
}

TEST_CASE("a stored record with a bad record_id is minted again", "[identity][service]") {
    Fixture f;
    json::object values = records_json({kAlice, kServer});
    values.at("records").as_array()[1].as_object()["record_id"] = "not-a-uuid";
    f.fs.write_text(f.accounts_path, envelope(3, std::move(values)));
    f.start();

    const IdentitySnapshot& identity = f.service->get();
    CHECK(identity.client == kAlice);
    CHECK(identity.host.record_id != kServer.record_id);
    CHECK(identity.host.display_name == "Alice");
    f.flush(f.accounts);
    CHECK(f.records_on_disk() == std::vector{kAlice, identity.host});
}

TEST_CASE("a host sharing the client's tag gets a fresh one, so the account ids differ", "[identity][service]") {
    Fixture f;
    AccountRecord copy = kServer;
    copy.tag = kAlice.tag;
    f.fs.write_text(f.accounts_path, envelope(3, records_json({kAlice, copy})));
    f.start();
    CHECK(f.service->get().client == kAlice);
    CHECK(f.service->get().host.record_id == kServer.record_id);
    CHECK(f.service->get().host.tag.size() == kTagLength);
    CHECK(account_id(f.service->get().host) != account_id(kAlice));
    f.flush(f.accounts);
    CHECK(f.records_on_disk() == std::vector{kAlice, f.service->get().host});
}

TEST_CASE("a host sharing the client's record_id is minted again", "[identity][service]") {
    Fixture f;
    AccountRecord copy = kServer;
    copy.record_id = kAlice.record_id;
    f.fs.write_text(f.accounts_path, envelope(3, records_json({kAlice, copy})));
    f.start();
    CHECK(f.service->get().client == kAlice);
    CHECK(f.service->get().host.record_id != kAlice.record_id);
}

TEST_CASE("extra records of a role are dropped on the write back", "[identity][service]") {
    Fixture f;
    AccountRecord second_client = kAlice;
    second_client.record_id = AccountRecordId{*parse_uuid("5d6e7f80-1a2b-4c3d-8e9f-a0b1c2d3e4f5")};
    second_client.tag = "q1w2e3";
    f.fs.write_text(f.accounts_path, envelope(3, records_json({kAlice, kServer, second_client})));
    f.start();
    CHECK(f.service->get() == IdentitySnapshot{kAlice, kServer});
    f.flush(f.accounts);
    CHECK(f.records_on_disk() == std::vector{kAlice, kServer});
}

TEST_CASE("a ReadOnly store keeps minted records in memory and refuses changes", "[identity][service]") {
    Fixture f;
    const std::string stored = envelope(AccountsDocument::kSchema + 1, 3, json::object{});
    f.fs.write_text(f.accounts_path, stored);
    f.fs.write_text(f.logins_path, envelope(BackendLoginsDocument::kSchema + 1, 3, json::object{}));
    f.start();
    REQUIRE(f.accounts.mode() == storage::StorageMode::ReadOnly);
    const IdentitySnapshot minted = f.service->get();
    CHECK(is_default_display_name(minted.display_name()));
    CHECK(f.fs.text(f.accounts_path) == stored);

    const Result<IdentitySnapshot> renamed = f.service->set_display_name("Alice");
    REQUIRE_FALSE(renamed);
    CHECK(renamed.error().id == "storage.read_only");
    const Result<IdentitySnapshot> reset = f.service->reset();
    REQUIRE_FALSE(reset);
    CHECK(reset.error().id == "storage.read_only");
    const Result<BackendLogin> login =
        f.service->set_backend_login(BackendLogin{endpoint("example.com"), "bob", CredentialPolicy::Ticket});
    REQUIRE_FALSE(login);
    CHECK(login.error().id == "storage.read_only");

    CHECK(f.service->get() == minted);
    CHECK(f.service->backend_login(endpoint("example.com")) == BackendLogin{endpoint("example.com")});
    CHECK(f.changes().empty());
}

TEST_CASE("a rename changes both account_ids but never a record_id or tag", "[identity][service]") {
    Fixture f;
    f.start();
    const IdentitySnapshot before = f.service->get();

    const Result<IdentitySnapshot> renamed = f.service->set_display_name("Alice");
    REQUIRE(renamed);
    CHECK(*renamed == named(before, "Alice"));
    CHECK(account_id(renamed->client) == "Alice-" + before.client.tag);
    CHECK(account_id(renamed->host) == "Alice-" + before.host.tag);
    CHECK(f.service->get() == *renamed);
    // A session that pinned the record keeps its copy.
    CHECK(before.display_name() != "Alice");

    f.flush(f.accounts);
    CHECK(f.records_on_disk() == std::vector{renamed->client, renamed->host});
    const std::vector<IdentityChangedEvent> changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].identity == *renamed);
}

TEST_CASE("an invalid name or the same name changes nothing", "[identity][service]") {
    Fixture f;
    f.start();
    f.flush(f.accounts);
    const u64 revision = f.accounts.revision();
    const IdentitySnapshot before = f.service->get();

    const Result<IdentitySnapshot> too_short = f.service->set_display_name("ab");
    REQUIRE_FALSE(too_short);
    CHECK(too_short.error().is(msg::kDisplayNameTooShort));
    const Result<IdentitySnapshot> invalid = f.service->set_display_name("Host Two");
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().is(msg::kDisplayNameInvalidCharacter));

    const Result<IdentitySnapshot> same = f.service->set_display_name(before.display_name());
    REQUIRE(same);
    CHECK(*same == before);

    CHECK(f.service->get() == before);
    CHECK(f.accounts.revision() == revision);
    CHECK(f.changes().empty());
}

TEST_CASE("reset gives a new default name and keeps the tags", "[identity][service]") {
    Fixture f;
    f.start();
    REQUIRE(f.service->set_display_name("Server1"));
    const IdentitySnapshot before = f.service->get();

    const Result<IdentitySnapshot> reset = f.service->reset();
    REQUIRE(reset);
    CHECK(is_default_display_name(reset->display_name()));
    CHECK(*reset == named(before, reset->display_name()));

    const Result<IdentitySnapshot> again = f.service->reset();
    REQUIRE(again);
    CHECK(again->display_name() != reset->display_name());
    f.flush(f.accounts);
    CHECK(f.records_on_disk() == std::vector{again->client, again->host});
}

TEST_CASE("identity changes coalesce", "[identity][service]") {
    Fixture f;
    f.start();
    REQUIRE(f.service->set_display_name("Alice"));
    REQUIRE(f.service->set_display_name("Alicia"));

    const std::vector<IdentityChangedEvent> changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].identity.display_name() == "Alicia");
    CHECK(f.recorder.sequence_ok());
}

TEST_CASE("a hand-edited name is adopted as a rename of both roles", "[identity][service]") {
    Fixture f;
    f.fs.write_text(f.accounts_path, envelope(3, records_json({kAlice, kServer})));
    f.start();

    json::object values = records_json({kAlice, kServer});
    values["display_name"] = "Edited";
    f.hand_edit_accounts(std::move(values));
    const IdentitySnapshot edited = named(IdentitySnapshot{kAlice, kServer}, "Edited");
    CHECK(f.service->get() == edited);
    const std::vector<IdentityChangedEvent> changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].identity == edited);
}

TEST_CASE("a hand edit that drops a record gets the one in memory written back", "[identity][service]") {
    Fixture f;
    f.fs.write_text(f.accounts_path, envelope(3, records_json({kAlice, kServer})));
    f.start();

    f.hand_edit_accounts({kAlice});
    f.flush(f.accounts);
    CHECK(f.service->get() == IdentitySnapshot{kAlice, kServer});
    CHECK(f.records_on_disk() == std::vector{kAlice, kServer});
    CHECK(f.changes().empty());
}

TEST_CASE("a hand edit with an invalid name or tag keeps the one in memory", "[identity][service]") {
    Fixture f;
    f.fs.write_text(f.accounts_path, envelope(3, records_json({kAlice, kServer})));
    f.start();

    json::object values = records_json({kAlice, kServer});
    values["display_name"] = "Alice Smith";
    values.at("records").as_array()[1].as_object()["tag"] = "TOOLONG1";
    f.hand_edit_accounts(std::move(values));
    f.flush(f.accounts);
    CHECK(f.service->get() == IdentitySnapshot{kAlice, kServer});
    CHECK(f.records_on_disk() == std::vector{kAlice, kServer});
    CHECK(f.changes().empty());

    // A valid name in the same edit is still a rename, and the bad tag still keeps the one in memory.
    values = records_json({kAlice, kServer});
    values["display_name"] = "Edited";
    values.at("records").as_array()[1].as_object()["tag"] = "";
    f.hand_edit_accounts(std::move(values));
    f.flush(f.accounts);
    const IdentitySnapshot edited = named(IdentitySnapshot{kAlice, kServer}, "Edited");
    CHECK(f.service->get() == edited);
    CHECK(f.records_on_disk() == std::vector{edited.client, edited.host});
}

TEST_CASE("a rename not yet written is laid over a hand edit made meanwhile", "[identity][service]") {
    Fixture f;
    f.fs.write_text(f.accounts_path, envelope(3, records_json({kAlice, kServer})));
    f.start();

    AccountRecord edited = kServer;
    edited.tag = "e1e2e3";
    f.fs.write_text(f.accounts_path, envelope(4, records_json({kAlice, edited})));
    const Result<IdentitySnapshot> renamed = f.service->set_display_name("Mine");
    REQUIRE(renamed);
    f.flush(f.accounts);

    // Only the name is unwritten, so it wins over the edit, whose host tag is adopted.
    CHECK(*renamed == named(IdentitySnapshot{kAlice, kServer}, "Mine"));
    const IdentitySnapshot mine = named(IdentitySnapshot{kAlice, edited}, "Mine");
    CHECK(f.service->get() == mine);
    CHECK(f.records_on_disk() == std::vector{mine.client, mine.host});
    const std::vector<IdentityChangedEvent> changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].identity == mine);
}

TEST_CASE("backend logins default per endpoint and are kept only when they differ", "[identity][service]") {
    Fixture f;
    f.start();
    CHECK(f.service->backend_login(endpoint("example.com")) == BackendLogin{endpoint("example.com")});

    const Result<BackendLogin> stored = f.service->set_backend_login(
        BackendLogin{HostPort{"Example.com", std::nullopt}, "bob@example.com", CredentialPolicy::LegacyArgv});
    REQUIRE(stored);
    CHECK(stored->endpoint == endpoint("example.com"));
    CHECK(f.service->backend_login(endpoint("example.com")) == *stored);
    CHECK(f.service->backend_login(HostPort{"EXAMPLE.COM", std::nullopt}) == *stored);
    // The client's effective_login may have changed.
    const std::vector<IdentityChangedEvent> changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].identity == f.service->get());

    REQUIRE(f.service->set_backend_login(BackendLogin{endpoint("example.com")}));
    CHECK(f.logins.get().logins.empty());
    f.flush(f.logins);
    const std::optional<std::string> text = f.fs.text(f.logins_path);
    REQUIRE(text);
    CHECK(json::parse(*text).at("values").at("logins").as_array().empty());
}

TEST_CASE("set_backend_login refuses an empty login or a bad endpoint", "[identity][service]") {
    Fixture f;
    f.start();
    const Result<BackendLogin> empty =
        f.service->set_backend_login(BackendLogin{endpoint("example.com"), "", CredentialPolicy::Ticket});
    REQUIRE_FALSE(empty);
    CHECK(empty.error().is(msg::kEmptyRemoteLogin));

    const Result<BackendLogin> bad_host =
        f.service->set_backend_login(BackendLogin{endpoint("not a host"), "bob", CredentialPolicy::Ticket});
    REQUIRE_FALSE(bad_host);
    CHECK(bad_host.error().id == "storage.invalid_host");

    CHECK(f.logins.get().logins.empty());
    CHECK(f.changes().empty());
}

TEST_CASE("login_target reads the login stored for the target's endpoint", "[identity][service]") {
    Fixture f;
    f.start();
    REQUIRE(f.service->set_backend_login(BackendLogin{endpoint("127.0.0.1"), "local", CredentialPolicy::LegacyArgv}));
    REQUIRE(f.service->set_backend_login(BackendLogin{endpoint("remote.example", 8443), "remote",
                                                      CredentialPolicy::Ticket}));

    storage::BackendTarget backend;
    const LoginTarget embedded = f.service->login_target(backend, UpstreamFlavor::Reboot, false);
    CHECK(embedded == LoginTarget{});

    backend.kind = BackendKind::Local;
    CHECK(f.service->login_target(backend, UpstreamFlavor::ThirdParty, true) ==
          LoginTarget{BackendKind::Local, UpstreamFlavor::ThirdParty, "local", CredentialPolicy::LegacyArgv, true});

    backend.kind = BackendKind::Remote;
    backend.remote = storage::RemoteBackendAddress{.scheme = std::nullopt,
                                                   .endpoint = HostPort{"Remote.Example", Port{8443}},
                                                   .xmpp = std::nullopt};
    CHECK(f.service->login_target(backend, UpstreamFlavor::Reboot, false) ==
          LoginTarget{BackendKind::Remote, UpstreamFlavor::Reboot, "remote", CredentialPolicy::Ticket, false});

    backend.remote->endpoint = endpoint("other.example");
    CHECK(f.service->login_target(backend, UpstreamFlavor::Reboot, false) ==
          LoginTarget{BackendKind::Remote, UpstreamFlavor::Reboot, std::nullopt, CredentialPolicy::Ticket, false});
}

TEST_CASE("a hand edit of the backend logins publishes the client record", "[identity][service]") {
    Fixture f;
    f.start();
    json::object values;
    values.emplace("logins", json::parse(R"([{"endpoint": {"host": "example.com", "port": 3551}, "login": "bob"}])"));
    f.fs.write_text(f.logins_path, envelope(1, 1, std::move(values)));
    f.logins.refresh();
    f.flush(f.logins);

    CHECK(f.service->backend_login(endpoint("example.com")).login == "bob");
    const std::vector<IdentityChangedEvent> changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].identity == f.service->get());
}

TEST_CASE("set_backend_login keeps the stored entry's unknown members", "[identity][service]") {
    Fixture f;
    json::object values;
    values.emplace("logins", json::parse(R"([{"endpoint": {"host": "example.com", "port": 3551}, "login": "bob",
                                                "future": true}])"));
    f.fs.write_text(f.logins_path, envelope(1, 1, std::move(values)));
    f.start();

    REQUIRE(f.service->set_backend_login(BackendLogin{endpoint("example.com"), "carol", CredentialPolicy::Ticket}));
    const BackendLogin changed = f.service->backend_login(endpoint("example.com"));
    CHECK(changed.login == "carol");
    CHECK(changed.unknown == json::object{{"future", true}});

    // Back to the defaults forgets the entry, unknown members included.
    REQUIRE(f.service->set_backend_login(BackendLogin{endpoint("example.com")}));
    CHECK(f.logins.get().logins.empty());
}

TEST_CASE("a destroyed service no longer handles hand edits", "[identity][service]") {
    Fixture f;
    f.fs.write_text(f.accounts_path, envelope(3, records_json({kAlice, kServer})));
    f.start();
    f.service.reset();

    f.hand_edit_accounts({kAlice});
    f.flush(f.accounts);
    CHECK(f.accounts.get().records.size() == 1);
    CHECK(f.records_on_disk() == std::vector{kAlice});
    CHECK(f.changes().empty());
}
