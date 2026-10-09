#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include "backend_test_support.hpp"
#include "reboot/backend/match_target_resolver.hpp"

using namespace rb;
using namespace rb::backend;
using namespace rb::backend::test;
using namespace std::chrono_literals;

namespace be = rb::contracts::backend;

namespace {

struct Resolver final : IMatchTargetResolver {
    ResolvedMatchTarget resolve(const MatchTargetQuery& query) override {
        queries.push_back(query.account_id + "/" + query.playlist);
        return answer;
    }

    ResolvedMatchTarget answer;
    std::vector<std::string> queries;
};

[[nodiscard]] LaunchCredentialRequest secret_request() {
    return LaunchCredentialRequest{"Player-abc123", be::CredentialKind::LaunchSecret, Changelist{1}};
}

}  // namespace

TEST_CASE("Ready is reported only once the replay was acknowledged", "[backend][process]") {
    ProcessHarness h;
    ProcessObserver observer;
    h.process->set_observer(&observer);
    h.script.child.reply_delay = 2s;
    h.process->register_account(AccountRegistration{"Player-abc123", record_id(1), AccountRole::Client});
    Captured<Result<void>> configured;
    h.process->configure_session(session_id(1), session_config("Player-abc123", "key-1"), configured.sink());
    h.rt.run_until_idle();
    REQUIRE(configured.value);
    CHECK(configured.value->has_value());

    REQUIRE(h.process->start(BackendConfig{BackendTarget{EmbeddedBackend{}}, true}));
    h.rt.run_until_idle();
    REQUIRE(h.backend().welcome());
    CHECK(h.backend().welcome()->bind_address == kLanBindAddress);
    CHECK(h.backend().welcome()->data_root == to_wire(backend_data_dir()));
    CHECK(h.backend().welcome()->log_level == LogLevel::Info);
    CHECK(observer.readies.empty());

    h.rt.advance(2s);
    REQUIRE(observer.readies.size() == 1);
    CHECK(observer.readies[0].generation == 1);
    CHECK(observer.readies[0].http_port == 1);
    CHECK(observer.readies[0].build == "fake");
    CHECK(h.backend().registered_accounts().size() == 1);
    REQUIRE(h.backend().live_sessions().size() == 1);
    const be::ConfigureSession sent = h.backend().live_sessions()[0];
    CHECK(sent.session_key == "key-1");
    CHECK(sent.account_id == "Player-abc123");
    CHECK(sent.origin == "http://127.0.0.1:4000/s/key-1/");
    CHECK(sent.console_key == "F8");
    CHECK(sent.build.version == "12.41");
    CHECK(sent.build.cl == 12345);
    CHECK(h.process->state() == process::ChildState::Running);
}

TEST_CASE("an ended session is no longer replayed", "[backend][process]") {
    ProcessHarness h;
    ProcessObserver observer;
    h.process->set_observer(&observer);
    REQUIRE(h.process->start(BackendConfig{}));
    h.rt.run_until_idle();
    REQUIRE(observer.readies.size() == 1);

    Captured<Result<void>> configured;
    h.process->configure_session(session_id(1), session_config("Player-abc123", "key-1"), configured.sink());
    h.rt.run_until_idle();
    REQUIRE(configured.value);
    CHECK(configured.value->has_value());
    CHECK(h.backend().live_sessions().size() == 1);

    Captured<Result<void>> ended;
    h.process->end_session(session_id(1), ended.sink());
    h.rt.run_until_idle();
    REQUIRE(ended.value);
    CHECK(ended.value->has_value());
    CHECK(h.backend().live_sessions().empty());

    Captured<Result<void>> unknown;
    h.process->end_session(session_id(9), unknown.sink());
    h.rt.run_until_idle();
    CHECK(unknown.calls == 1);
}

TEST_CASE("a rename waits for the sessions on the old id, and the new id waits for the rename", "[backend][process]") {
    ProcessHarness h;
    ProcessObserver observer;
    h.process->set_observer(&observer);
    h.process->register_account(AccountRegistration{"Old-abc123", record_id(1), AccountRole::Client});
    h.process->configure_session(session_id(1), session_config("Old-abc123", "key-1"), nullptr);
    REQUIRE(h.process->start(BackendConfig{}));
    h.rt.run_until_idle();
    REQUIRE(observer.readies.size() == 1);

    Captured<Result<void>> renamed;
    h.process->rename_account("Old-abc123", "New-abc123", be::RenameConflictPolicy::Report, renamed.sink());
    h.process->register_account(AccountRegistration{"New-abc123", record_id(1), AccountRole::Client});
    h.rt.run_until_idle();
    CHECK(renamed.calls == 0);
    REQUIRE(h.backend().registered_accounts().size() == 1);
    CHECK(h.backend().registered_accounts()[0].account_id == "Old-abc123");

    h.process->end_session(session_id(1), nullptr);
    h.rt.run_until_idle();
    REQUIRE(renamed.value);
    CHECK(renamed.value->has_value());
    REQUIRE(h.backend().registered_accounts().size() == 1);
    CHECK(h.backend().registered_accounts()[0].account_id == "New-abc123");
}

TEST_CASE("a rename asked for while stopped is replayed before the registrations", "[backend][process]") {
    ProcessHarness h;
    ProcessObserver observer;
    h.process->set_observer(&observer);
    REQUIRE(h.process->start(BackendConfig{}));
    h.process->register_account(AccountRegistration{"Old-abc123", record_id(1), AccountRole::Client});
    h.rt.run_until_idle();
    REQUIRE(h.process->stop(0ms));
    h.rt.run_until_idle();

    Captured<Result<void>> renamed;
    h.process->rename_account("Old-abc123", "New-abc123", be::RenameConflictPolicy::Report, renamed.sink());
    h.process->register_account(AccountRegistration{"New-abc123", record_id(1), AccountRole::Client});
    h.rt.run_until_idle();
    CHECK(renamed.calls == 0);

    // A fresh backend has no Old account, so the rename fails and the registration follows it.
    REQUIRE(h.process->start(BackendConfig{}));
    h.rt.run_until_idle();
    REQUIRE(renamed.value);
    CHECK_FALSE(renamed.value->has_value());
    REQUIRE(observer.readies.size() == 2);
    REQUIRE(h.backend().registered_accounts().size() == 1);
    CHECK(h.backend().registered_accounts()[0].account_id == "New-abc123");
}

TEST_CASE("a rename conflict reaches the handler before the rename fails", "[backend][process]") {
    ProcessHarness h;
    std::vector<std::string> conflicts;
    h.process->set_rename_conflict_handler(
        [&](const be::AccountRenameConflict& conflict) { conflicts.push_back(conflict.new_account_id); });
    h.process->register_account(AccountRegistration{"Old-abc123", record_id(1), AccountRole::Client});
    h.process->register_account(AccountRegistration{"New-def456", record_id(2), AccountRole::Host});
    REQUIRE(h.process->start(BackendConfig{}));
    h.rt.run_until_idle();

    bool failed = false;
    h.process->rename_account("Old-abc123", "New-def456", be::RenameConflictPolicy::Report, [&](Result<void> result) {
        failed = !result;
        CHECK(conflicts.size() == 1);
    });
    h.rt.run_until_idle();
    CHECK(failed);
    CHECK(conflicts == std::vector<std::string>{"New-def456"});
}

TEST_CASE("a crash restarts the backend and the replay goes to the new generation only", "[backend][process]") {
    ProcessHarness h;
    ProcessObserver observer;
    h.process->set_observer(&observer);
    testing::FakeBackendScript crashing;
    crashing.child.reply_delay = 2s;
    crashing.child.crash = testing::ScriptedFailure{testing::ScriptStage::Ready, 1s};
    h.scripts.push_back(crashing);
    h.process->register_account(AccountRegistration{"Player-abc123", record_id(1), AccountRole::Client});
    h.process->configure_session(session_id(1), session_config("Player-abc123", "key-1"), nullptr);
    REQUIRE(h.process->start(BackendConfig{}));
    h.rt.run_until_idle();
    CHECK(observer.readies.empty());

    h.rt.advance(1s);
    REQUIRE(observer.exits.size() == 1);
    CHECK(observer.exits[0].cause == process::ChildExitCause::Exited);
    CHECK(observer.exits[0].after == process::AfterExit::Restarting);

    h.rt.advance(5s);
    REQUIRE(observer.readies.size() == 1);
    CHECK(observer.readies[0].generation == 2);
    CHECK(h.process->generation() == 2);
    REQUIRE(h.backends.size() == 2);
    CHECK(h.backend().registered_accounts().size() == 1);
    CHECK(h.backend().live_sessions().size() == 1);
}

TEST_CASE("a requested stop ends with cause Requested", "[backend][process]") {
    ProcessHarness h;
    ProcessObserver observer;
    h.process->set_observer(&observer);
    CHECK_FALSE(h.process->stop());
    REQUIRE(h.process->start(BackendConfig{}));
    h.rt.run_until_idle();
    REQUIRE(h.process->pid());
    REQUIRE(h.process->stop());
    h.rt.run_until_idle();
    REQUIRE(observer.exits.size() == 1);
    CHECK(observer.exits[0].cause == process::ChildExitCause::Requested);
    CHECK(h.process->state() == process::ChildState::Stopped);
    CHECK(h.records.size() == 2);
}

TEST_CASE("match targets are answered from the resolver, or with no endpoint", "[backend][process]") {
    ProcessHarness h;
    Resolver resolver;
    resolver.answer = ResolvedMatchTarget{HostPort{"10.0.0.2", Port{7777}}, Port{7778}};
    h.script.match_target_requests = {be::ResolveMatchTarget{0, "Player-abc123", "playlist_defaultsolo"}};
    h.process->set_match_target_resolver(&resolver);
    REQUIRE(h.process->start(BackendConfig{}));
    h.rt.run_until_idle();
    CHECK(resolver.queries == std::vector<std::string>{"Player-abc123/playlist_defaultsolo"});
    REQUIRE(h.backend().match_targets().size() == 1);
    CHECK(h.backend().match_targets()[0].endpoint == "10.0.0.2:7777");
    CHECK(h.backend().match_targets()[0].beacon_port == 7778);

    h.process->set_match_target_resolver(nullptr);
    REQUIRE(h.process->stop(0ms));
    h.rt.run_until_idle();
    REQUIRE(h.process->start(BackendConfig{}));
    h.rt.run_until_idle();
    REQUIRE(h.backend().match_targets().size() == 1);
    CHECK_FALSE(h.backend().match_targets()[0].endpoint);
}

TEST_CASE("an observed login names its session, and an unknown key is dropped", "[backend][process]") {
    ProcessHarness h;
    ProcessObserver observer;
    h.process->set_observer(&observer);
    h.script.logins = {be::LoginObserved{"Player-abc123", "key-1"}, be::LoginObserved{"Stranger", "other"}};
    h.process->configure_session(session_id(1), session_config("Player-abc123", "key-1"), nullptr);
    REQUIRE(h.process->start(BackendConfig{}));
    h.rt.run_until_idle();
    REQUIRE(observer.logins.size() == 1);
    CHECK(observer.logins[0].session == session_id(1));
    CHECK(observer.logins[0].account_id == "Player-abc123");
}

TEST_CASE("a launch credential needs a configured session and is redacted on receipt", "[backend][process]") {
    ProcessHarness h;
    REQUIRE(h.process->start(BackendConfig{}));
    h.rt.run_until_idle();

    Captured<Result<LaunchCredential>> unconfigured;
    h.process->mint_launch_credential(session_id(1), secret_request(),
                                      unconfigured.sink());
    h.rt.run_until_idle();
    REQUIRE(unconfigured.value);
    REQUIRE_FALSE(unconfigured.value->has_value());
    CHECK(unconfigured.value->error().id == "internal.bug");

    h.process->configure_session(session_id(1), session_config("Player-abc123", "key-1"), nullptr);
    Captured<Result<LaunchCredential>> minted;
    h.process->mint_launch_credential(session_id(1), secret_request(),
                                      minted.sink());
    h.rt.run_until_idle();
    REQUIRE(minted.value);
    REQUIRE(minted.value->has_value());
    const std::string value = (*minted.value)->value.reveal();
    CHECK(value.starts_with("fake-secret-"));
    CHECK((*minted.value)->expires_at == h.rt.clock().system_now() + 5min);
    CHECK(h.redactor.apply("token " + value).find(value) == std::string::npos);
    CHECK(h.backend().credentials_minted() == 1);
}

TEST_CASE("accounts are listed with their kind, and prune answers with what it removed", "[backend][process]") {
    ProcessHarness h;
    h.script.logins = {be::LoginObserved{"Player-abc123", "key-1"}};
    h.process->register_account(AccountRegistration{"Player-abc123", record_id(1), AccountRole::Client});
    h.process->register_account(AccountRegistration{"Host-abc123", record_id(2), AccountRole::Host});
    REQUIRE(h.process->start(BackendConfig{}));
    h.rt.run_until_idle();

    Captured<Result<std::vector<BackendAccount>>> listed;
    h.process->list_accounts(listed.sink());
    h.rt.run_until_idle();
    REQUIRE(listed.value);
    REQUIRE(listed.value->has_value());
    REQUIRE((*listed.value)->size() == 2);
    for (const BackendAccount& account : **listed.value) {
        CHECK(account.kind == BackendAccountKind::Local);
        CHECK(account.record);
        CHECK(account.last_login.has_value() == (account.account_id == "Player-abc123"));
    }

    Captured<Result<std::vector<BackendAccount>>> pruned;
    h.process->prune_accounts(AccountPruneFilter{h.rt.clock().system_now() + 1h, std::nullopt}, pruned.sink());
    h.rt.run_until_idle();
    REQUIRE(pruned.value);
    REQUIRE(pruned.value->has_value());
    REQUIRE((*pruned.value)->size() == 1);
    CHECK((*pruned.value)->front().account_id == "Player-abc123");
    CHECK(h.backend().registered_accounts().size() == 1);
}

TEST_CASE("requests need a running backend", "[backend][process]") {
    ProcessHarness h;
    Captured<Result<BackendHealth>> health;
    h.process->health(health.sink());
    h.rt.run_until_idle();
    REQUIRE(health.value);
    REQUIRE_FALSE(health.value->has_value());
    CHECK(health.value->error().id == "process.child_not_running");

    REQUIRE(h.process->start(BackendConfig{}));
    h.rt.run_until_idle();
    Captured<Result<BackendHealth>> running;
    h.process->health(running.sink());
    h.rt.run_until_idle();
    REQUIRE(running.value);
    REQUIRE(running.value->has_value());
    CHECK((*running.value)->http_port == 1);
    CHECK((*running.value)->version == "fake");
}

TEST_CASE("a launch credential is minted only for the session's own account", "[backend][process]") {
    ProcessHarness h;
    REQUIRE(h.process->start(BackendConfig{}));
    h.process->configure_session(session_id(1), session_config("Player-abc123", "key-1"), nullptr);
    h.rt.run_until_idle();

    Captured<Result<LaunchCredential>> minted;
    h.process->mint_launch_credential(
        session_id(1), LaunchCredentialRequest{"Other-def456", be::CredentialKind::LaunchSecret, Changelist{1}},
        minted.sink());
    h.rt.run_until_idle();
    REQUIRE(minted.value);
    REQUIRE_FALSE(minted.value->has_value());
    CHECK(minted.value->error().id == "internal.bug");
    CHECK(h.backend().credentials_minted() == 0);
}

TEST_CASE("a rename the backend died answering goes to the next generation", "[backend][process]") {
    ProcessHarness h;
    testing::FakeBackendScript crashing;
    crashing.child.reply_delay = 2s;
    crashing.child.crash = testing::ScriptedFailure{testing::ScriptStage::Ready, 1s};
    h.scripts.push_back(crashing);
    REQUIRE(h.process->start(BackendConfig{}));
    h.rt.run_until_idle();

    Captured<Result<void>> renamed;
    h.process->rename_account("Old-abc123", "New-abc123", be::RenameConflictPolicy::Report, renamed.sink());
    h.rt.advance(1s);
    CHECK(renamed.calls == 0);

    h.rt.advance(5s);
    REQUIRE(h.backends.size() == 2);
    // The fresh backend has no Old account, so the retried rename fails there rather than with the dead child.
    REQUIRE(renamed.value);
    REQUIRE_FALSE(renamed.value->has_value());
    CHECK(renamed.value->error().id != "process.child_gone");
    const testing::ScriptedChild* child = h.launcher.last(kBackendExe);
    REQUIRE(child != nullptr);
    CHECK(child->stdin_frames().count(contract_frame_type_v<be::RenameAccount>) == 1);
}
