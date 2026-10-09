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

using namespace reboot;
using namespace reboot::identity;
using storage::AccountsDocument;
using storage::BackendKind;
namespace json = boost::json;

namespace {

const AccountRecord kAlice{AccountRecordId{*parse_uuid("9a3e7c51-2d44-4c1b-8f0a-61b2d5e9c3f7")}, AccountRole::Client,
                           "Alice", "a1b2c3"};
const AccountRecord kServer{AccountRecordId{*parse_uuid("1c0b8a6e-5f3d-4e2a-9b7c-0d1e2f3a4b5c")}, AccountRole::Host,
                            "Server1", "z9y8x7"};

[[nodiscard]] std::string envelope(u32 schema, u64 revision, json::object values) {
    json::object out;
    out.emplace("schema", schema);
    out.emplace("revision", revision);
    out.emplace("values", std::move(values));
    return json::serialize(out);
}

[[nodiscard]] json::object records_json(std::vector<AccountRecord> records) {
    AccountsDocument document;
    document.records = std::move(records);
    return document.write();
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
    void hand_edit_accounts(std::vector<AccountRecord> records) {
        fs.write_text(accounts_path, envelope(1, accounts.revision() + 1, records_json(std::move(records))));
        accounts.refresh();
        flush(accounts);
    }

    [[nodiscard]] AccountsDocument accounts_on_disk() const {
        const std::optional<std::string> text = fs.text(accounts_path);
        REQUIRE(text);
        std::vector<storage::ValueIssue> issues;
        AccountsDocument document = AccountsDocument::read(json::parse(*text).at("values").as_object(), issues);
        CHECK(issues.empty());
        return document;
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

TEST_CASE("ensure_records mints a client and a host record and writes them", "[identity][service]") {
    Fixture f;
    f.start();
    const IdentitySnapshot& identity = f.service->get();
    CHECK(identity.client.role == AccountRole::Client);
    CHECK(identity.host.role == AccountRole::Host);
    CHECK(is_default_display_name(identity.client.display_name, AccountRole::Client));
    CHECK(is_default_display_name(identity.host.display_name, AccountRole::Host));
    CHECK(identity.client.tag.size() == kTagLength);
    CHECK(identity.host.tag.size() == kTagLength);
    CHECK(identity.client.record_id != identity.host.record_id);
    CHECK(f.service->record(AccountRole::Host) == identity.host);

    f.flush(f.accounts);
    CHECK(f.accounts_on_disk().records == std::vector{identity.client, identity.host});
    // Startup sets the identity; there is no earlier one to change.
    CHECK(f.changes().empty());
}

TEST_CASE("stored records are kept and not written again", "[identity][service]") {
    Fixture f;
    const std::string stored = envelope(1, 3, records_json({kServer, kAlice}));
    f.fs.write_text(f.accounts_path, stored);
    f.start();
    CHECK(f.service->get() == IdentitySnapshot{kAlice, kServer});
    CHECK(f.accounts.revision() == 3u);
    f.flush(f.accounts);
    CHECK(f.fs.text(f.accounts_path) == stored);
}

TEST_CASE("a record storage dropped as invalid is minted again", "[identity][service]") {
    Fixture f;
    json::object values = records_json({kAlice, kServer});
    values.at("records").as_array()[1].as_object()["tag"] = "NOT-OK";
    f.fs.write_text(f.accounts_path, envelope(1, 3, std::move(values)));
    f.start();

    const IdentitySnapshot& identity = f.service->get();
    CHECK(identity.client == kAlice);
    CHECK(identity.host.record_id != kServer.record_id);
    CHECK(is_default_display_name(identity.host.display_name, AccountRole::Host));
    f.flush(f.accounts);
    CHECK(f.accounts_on_disk().records == std::vector{kAlice, identity.host});
}

TEST_CASE("a host sharing the client's record_id is minted again", "[identity][service]") {
    Fixture f;
    AccountRecord copy = kServer;
    copy.record_id = kAlice.record_id;
    f.fs.write_text(f.accounts_path, envelope(1, 3, records_json({kAlice, copy})));
    f.start();
    CHECK(f.service->get().client == kAlice);
    CHECK(f.service->get().host.record_id != kAlice.record_id);
}

TEST_CASE("extra records of a role are dropped on the write back", "[identity][service]") {
    Fixture f;
    AccountRecord second_client = kAlice;
    second_client.record_id = AccountRecordId{*parse_uuid("5d6e7f80-1a2b-4c3d-8e9f-a0b1c2d3e4f5")};
    second_client.display_name = "Bob";
    f.fs.write_text(f.accounts_path, envelope(1, 3, records_json({kAlice, kServer, second_client})));
    f.start();
    CHECK(f.service->get() == IdentitySnapshot{kAlice, kServer});
    f.flush(f.accounts);
    CHECK(f.accounts_on_disk().records == std::vector{kAlice, kServer});
}

TEST_CASE("a ReadOnly store keeps minted records in memory and refuses changes", "[identity][service]") {
    Fixture f;
    const std::string stored = envelope(2, 3, json::object{});
    f.fs.write_text(f.accounts_path, stored);
    f.fs.write_text(f.logins_path, stored);
    f.start();
    REQUIRE(f.accounts.mode() == storage::StorageMode::ReadOnly);
    const IdentitySnapshot minted = f.service->get();
    CHECK(is_default_display_name(minted.client.display_name, AccountRole::Client));
    CHECK(f.fs.text(f.accounts_path) == stored);

    const Result<AccountRecord> renamed = f.service->set_display_name(AccountRole::Client, "Alice");
    REQUIRE_FALSE(renamed);
    CHECK(renamed.error().id == "storage.read_only");
    const Result<AccountRecord> reset = f.service->reset(AccountRole::Host);
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

TEST_CASE("a rename changes account_id but never record_id or tag", "[identity][service]") {
    Fixture f;
    f.start();
    const AccountRecord before = f.service->record(AccountRole::Client);
    const AccountRecord host = f.service->record(AccountRole::Host);

    const Result<AccountRecord> renamed = f.service->set_display_name(AccountRole::Client, "Alice");
    REQUIRE(renamed);
    CHECK(renamed->display_name == "Alice");
    CHECK(renamed->record_id == before.record_id);
    CHECK(renamed->tag == before.tag);
    CHECK(account_id(*renamed) == "Alice-" + before.tag);
    CHECK(f.service->record(AccountRole::Client) == *renamed);
    CHECK(f.service->record(AccountRole::Host) == host);
    // A session that pinned the record keeps its copy.
    CHECK(before.display_name != "Alice");

    f.flush(f.accounts);
    CHECK(f.accounts_on_disk().records == std::vector{*renamed, host});
    const std::vector<IdentityChangedEvent> changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].record == *renamed);
}

TEST_CASE("an invalid name or the same name changes nothing", "[identity][service]") {
    Fixture f;
    f.start();
    f.flush(f.accounts);
    const u64 revision = f.accounts.revision();
    const AccountRecord before = f.service->record(AccountRole::Host);

    const Result<AccountRecord> too_short = f.service->set_display_name(AccountRole::Host, "ab");
    REQUIRE_FALSE(too_short);
    CHECK(too_short.error().is(msg::kDisplayNameTooShort));
    const Result<AccountRecord> invalid = f.service->set_display_name(AccountRole::Host, "Host Two");
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().is(msg::kDisplayNameInvalidCharacter));

    const Result<AccountRecord> same = f.service->set_display_name(AccountRole::Host, before.display_name);
    REQUIRE(same);
    CHECK(*same == before);

    CHECK(f.service->record(AccountRole::Host) == before);
    CHECK(f.accounts.revision() == revision);
    CHECK(f.changes().empty());
}

TEST_CASE("reset gives a new default name and keeps the tag", "[identity][service]") {
    Fixture f;
    f.start();
    REQUIRE(f.service->set_display_name(AccountRole::Host, "Server1"));
    const AccountRecord named = f.service->record(AccountRole::Host);

    const Result<AccountRecord> reset = f.service->reset(AccountRole::Host);
    REQUIRE(reset);
    CHECK(is_default_display_name(reset->display_name, AccountRole::Host));
    CHECK(reset->tag == named.tag);
    CHECK(reset->record_id == named.record_id);

    const Result<AccountRecord> again = f.service->reset(AccountRole::Host);
    REQUIRE(again);
    CHECK(again->display_name != reset->display_name);
    f.flush(f.accounts);
    CHECK(f.accounts_on_disk().records.back() == *again);
}

TEST_CASE("record changes coalesce per role", "[identity][service]") {
    Fixture f;
    f.start();
    REQUIRE(f.service->set_display_name(AccountRole::Client, "Alice"));
    REQUIRE(f.service->set_display_name(AccountRole::Host, "Server1"));
    REQUIRE(f.service->set_display_name(AccountRole::Client, "Alicia"));

    const std::vector<IdentityChangedEvent> changes = f.changes();
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].record.display_name == "Server1");
    CHECK(changes[1].record.display_name == "Alicia");
    CHECK(f.recorder.sequence_ok());
}

TEST_CASE("a hand-edited name is adopted as a rename", "[identity][service]") {
    Fixture f;
    f.fs.write_text(f.accounts_path, envelope(1, 3, records_json({kAlice, kServer})));
    f.start();

    AccountRecord edited = kAlice;
    edited.display_name = "Edited";
    f.hand_edit_accounts({edited, kServer});
    CHECK(f.service->get() == IdentitySnapshot{edited, kServer});
    const std::vector<IdentityChangedEvent> changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].record == edited);
}

TEST_CASE("a hand edit that drops a record gets it minted again and written back", "[identity][service]") {
    Fixture f;
    f.fs.write_text(f.accounts_path, envelope(1, 3, records_json({kAlice, kServer})));
    f.start();

    f.hand_edit_accounts({kAlice});
    f.flush(f.accounts);
    const AccountRecord host = f.service->record(AccountRole::Host);
    CHECK(host.record_id != kServer.record_id);
    CHECK(is_default_display_name(host.display_name, AccountRole::Host));
    CHECK(f.service->record(AccountRole::Client) == kAlice);
    CHECK(f.accounts_on_disk().records == std::vector{kAlice, host});
    const std::vector<IdentityChangedEvent> changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].record == host);
}

TEST_CASE("a rename not yet written is laid over a hand edit made meanwhile", "[identity][service]") {
    Fixture f;
    f.fs.write_text(f.accounts_path, envelope(1, 3, records_json({kAlice, kServer})));
    f.start();

    AccountRecord edited = kServer;
    edited.display_name = "Edited";
    f.fs.write_text(f.accounts_path, envelope(1, 4, records_json({kAlice, edited})));
    const Result<AccountRecord> renamed = f.service->set_display_name(AccountRole::Client, "Mine");
    REQUIRE(renamed);
    f.flush(f.accounts);

    // The engine's unwritten records win, so the edit's host name is gone and nothing else changed.
    CHECK(f.service->get() == IdentitySnapshot{*renamed, kServer});
    CHECK(f.accounts_on_disk().records == std::vector{*renamed, kServer});
    const std::vector<IdentityChangedEvent> changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].record == *renamed);
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
    CHECK(changes[0].record == f.service->get().client);

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
    CHECK(changes[0].record == f.service->get().client);
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
    f.fs.write_text(f.accounts_path, envelope(1, 3, records_json({kAlice, kServer})));
    f.start();
    f.service.reset();

    f.hand_edit_accounts({kAlice});
    f.flush(f.accounts);
    CHECK(f.accounts.get().records == std::vector{kAlice});
    CHECK(f.accounts_on_disk().records == std::vector{kAlice});
    CHECK(f.changes().empty());
}
