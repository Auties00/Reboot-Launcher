#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <any>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "backend_test_support.hpp"
#include "reboot/backend/account_rename_conflict.hpp"
#include "reboot/backend/backend_accounts.hpp"
#include "reboot/backend/backend_accounts_changed.hpp"
#include "reboot/backend/backend_service.hpp"
#include "reboot/backend/backend_sessions.hpp"
#include "reboot/backend/remote_backend_probe.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_random.hpp"

using namespace rb;
using namespace rb::backend;
using namespace rb::backend::test;
using namespace std::chrono_literals;

namespace {

struct NoSessions final : IBackendSessions {
    void stop_sessions(std::vector<SessionId>, UniqueFunction<void(Result<void>)> done) override { done({}); }
};

[[nodiscard]] identity::AccountRecord record(u8 tag, AccountRole role, std::string name) {
    identity::AccountRecord out;
    out.record_id = record_id(tag);
    out.role = role;
    out.display_name = std::move(name);
    out.tag = "aaaaa" + std::to_string(tag);
    return out;
}

struct AccountsHarness : ProcessHarness {
    explicit AccountsHarness(BackendConfig config = {}) {
        service = std::make_unique<BackendService>(std::move(config), *process, probe, sessions, rt.ops(), rt.requests(),
                                                   tls, rt.events(), rt.strand(), rt.timers());
        accounts = std::make_unique<BackendAccounts>(*service, *process, rt.ops(), rt.requests(), rt.events());
        changes = rt.events().subscribe(EventFilter{{EventKind::BackendAccountsChanged}, std::nullopt, std::nullopt}, 1u << 20);
    }

    ~AccountsHarness() {
        accounts.reset();
        service.reset();
    }

    [[nodiscard]] std::optional<ErasedOutcome> outcome(const Result<OpHandle>& handle) {
        REQUIRE(handle);
        return rt.ops().outcome(handle->id());
    }

    template <class T>
    [[nodiscard]] std::optional<T> completed(const Result<OpHandle>& handle) {
        const std::optional<ErasedOutcome> done = outcome(handle);
        if (!done) return std::nullopt;
        const auto* value = std::get_if<Completed<std::any>>(&*done);
        if (value == nullptr) return std::nullopt;
        return std::any_cast<T>(value->value);
    }

    [[nodiscard]] bool succeeded(const Result<OpHandle>& handle) {
        const std::optional<ErasedOutcome> done = outcome(handle);
        return done && std::holds_alternative<Completed<std::any>>(*done);
    }

    [[nodiscard]] std::vector<std::string> ids() const {
        std::vector<std::string> out;
        for (const contracts::backend::RegisterAccount& account : backends.back()->registered_accounts())
            out.push_back(account.account_id);
        std::ranges::sort(out);
        return out;
    }

    [[nodiscard]] std::optional<UserRequest> pending(UserRequestKind kind) {
        for (const UserRequest& request : rt.requests().pending())
            if (request.kind == kind) return request;
        return std::nullopt;
    }

    [[nodiscard]] std::size_t accounts_changed() {
        std::vector<EventEnvelope> drained;
        changes->drain(drained, 64);
        published += drained.size();
        return published;
    }

    testing::FakeHttpTransport transport{rt.strand(), rt.clock()};
    testing::FakeRandom random{9};
    net::HostTlsMemory tls{{}, nullptr};
    net::HttpClient http{transport, tls, rt.strand(), rt.timers(), random};
    RemoteBackendProbe probe{http};
    NoSessions sessions;
    std::unique_ptr<BackendService> service;
    std::unique_ptr<BackendAccounts> accounts;
    std::shared_ptr<Subscription> changes;
    std::size_t published = 0;
};

const identity::IdentitySnapshot kIdentity{record(1, AccountRole::Client, "Alpha"), record(2, AccountRole::Host, "Beta")};

}  // namespace

TEST_CASE("the list loads once the backend runs and every op refreshes it", "[backend][accounts]") {
    AccountsHarness h;
    h.accounts->register_identity(kIdentity);
    const Result<std::vector<BackendAccount>> early = h.accounts->list();
    REQUIRE_FALSE(early);
    CHECK(early.error().id == "backend.accounts_not_loaded");

    const Result<OpHandle> reset = h.accounts->start_reset("Alpha-aaaaa1", DisconnectPolicy::Detached);
    REQUIRE(reset);
    h.rt.run_until_idle();
    CHECK(h.succeeded(reset));
    const Result<std::vector<BackendAccount>> listed = h.accounts->list();
    REQUIRE(listed);
    REQUIRE(listed->size() == 2);
    CHECK(std::ranges::all_of(*listed, [](const BackendAccount& a) { return a.kind == BackendAccountKind::Local; }));
    CHECK(h.accounts_changed() >= 1);
    // The maintenance lease was the only one, so the backend stops with it.
    CHECK(h.service->state().phase == BackendPhase::Stopped);
    CHECK(h.service->state().leases == 0);
}

TEST_CASE("delete removes the account and publishes the new list", "[backend][accounts]") {
    AccountsHarness h;
    h.accounts->register_identity(kIdentity);
    REQUIRE(h.service->start_backend(true, DisconnectPolicy::Detached));
    h.rt.run_until_idle();
    const Result<OpHandle> deleted = h.accounts->start_delete("Beta-aaaaa2", DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    CHECK(h.succeeded(deleted));
    CHECK(h.ids() == std::vector<std::string>{"Alpha-aaaaa1"});
    const Result<std::vector<BackendAccount>> listed = h.accounts->list();
    REQUIRE(listed);
    REQUIRE(listed->size() == 1);
    CHECK(listed->front().account_id == "Alpha-aaaaa1");
    CHECK(h.service->state().phase == BackendPhase::Running);
}

TEST_CASE("an identity rename moves the backend data once no session uses the old id", "[backend][accounts]") {
    AccountsHarness h;
    h.accounts->register_identity(kIdentity);
    REQUIRE(h.service->start_backend(true, DisconnectPolicy::Detached));
    std::optional<BackendLease> lease = h.service->acquire(session_id(1)).value();
    h.service->configure_session(*lease, session_config("Alpha-aaaaa1", "key-1"), nullptr);
    h.rt.run_until_idle();

    h.accounts->on_identity_changed(identity::IdentityChangedEvent{record(1, AccountRole::Client, "Gamma")});
    h.rt.run_until_idle();
    CHECK(h.ids() == std::vector<std::string>{"Alpha-aaaaa1", "Beta-aaaaa2"});

    lease.reset();
    h.rt.run_until_idle();
    CHECK(h.ids() == std::vector<std::string>{"Beta-aaaaa2", "Gamma-aaaaa1"});
    const Result<std::vector<BackendAccount>> listed = h.accounts->list();
    REQUIRE(listed);
    CHECK(std::ranges::any_of(*listed, [](const BackendAccount& a) { return a.account_id == "Gamma-aaaaa1"; }));
}

TEST_CASE("an identity rename onto an existing account asks which data to keep", "[backend][accounts]") {
    AccountsHarness h;
    h.accounts->register_identity(kIdentity);
    REQUIRE(h.service->start_backend(true, DisconnectPolicy::Detached));
    h.rt.run_until_idle();
    // Data under the new id, left by an earlier account of that name.
    h.process->register_account(AccountRegistration{"Delta-aaaaa1", record_id(9), AccountRole::Client});
    h.rt.run_until_idle();

    h.accounts->on_identity_changed(identity::IdentityChangedEvent{record(1, AccountRole::Client, "Delta")});
    h.rt.run_until_idle();
    const std::optional<UserRequest> asked = h.pending(UserRequestKind::AccountRenameConflict);
    REQUIRE(asked);
    CHECK_FALSE(asked->op);
    const auto& prompt = std::any_cast<const AccountRenameConflictPrompt&>(asked->payload);
    CHECK(prompt.old_account_id == "Alpha-aaaaa1");
    CHECK(prompt.new_account_id == "Delta-aaaaa1");

    CHECK_FALSE(h.rt.requests().respond(asked->id, AccountRenameConflictAnswer{RenameConflictChoice::Ask}));
    REQUIRE(h.rt.requests().respond(asked->id, AccountRenameConflictAnswer{RenameConflictChoice::Replace}));
    h.rt.run_until_idle();
    CHECK(h.ids() == std::vector<std::string>{"Beta-aaaaa2", "Delta-aaaaa1"});
}

TEST_CASE("a rename op asks on conflict and completes with the new id", "[backend][accounts]") {
    AccountsHarness h;
    h.accounts->register_identity(kIdentity);
    REQUIRE(h.service->start_backend(true, DisconnectPolicy::Detached));
    h.rt.run_until_idle();

    const Result<OpHandle> renamed =
        h.accounts->start_rename(AccountRenameRequest{"Alpha-aaaaa1", "Beta-aaaaa2", RenameConflictChoice::Ask},
                                 DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    const std::optional<UserRequest> asked = h.pending(UserRequestKind::AccountRenameConflict);
    REQUIRE(asked);
    CHECK(asked->op == renamed->id());
    CHECK(std::any_cast<const AccountRenameConflictPrompt&>(asked->payload).existing_record == record_id(2));
    CHECK_FALSE(h.outcome(renamed));

    REQUIRE(h.rt.requests().respond(asked->id, AccountRenameConflictAnswer{RenameConflictChoice::KeepExisting}));
    h.rt.run_until_idle();
    CHECK(h.completed<std::string>(renamed) == "Beta-aaaaa2");
    CHECK(h.ids() == std::vector<std::string>{"Beta-aaaaa2"});
}

TEST_CASE("a rename op that is told what to do on conflict does not ask", "[backend][accounts]") {
    AccountsHarness h;
    h.accounts->register_identity(kIdentity);
    REQUIRE(h.service->start_backend(true, DisconnectPolicy::Detached));
    h.rt.run_until_idle();
    const Result<OpHandle> renamed =
        h.accounts->start_rename(AccountRenameRequest{"Alpha-aaaaa1", "Beta-aaaaa2", RenameConflictChoice::Replace},
                                 DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    CHECK_FALSE(h.pending(UserRequestKind::AccountRenameConflict));
    CHECK(h.completed<std::string>(renamed) == "Beta-aaaaa2");
}

TEST_CASE("prune checks what the backend removed against the filter", "[backend][accounts]") {
    AccountsHarness h;
    h.script.logins = {contracts::backend::LoginObserved{"Alpha-aaaaa1", "key"}};
    h.accounts->register_identity(kIdentity);
    REQUIRE(h.service->start_backend(true, DisconnectPolicy::Detached));
    h.rt.run_until_idle();

    const Result<OpHandle> nothing = h.accounts->start_prune(
        AccountPruneFilter{h.rt.clock().system_now() - 1h, std::nullopt}, DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    CHECK(h.completed<u64>(nothing) == 0u);

    // FakeBackend prunes registered accounts, which are Local and never selected.
    const Result<OpHandle> wrong = h.accounts->start_prune(
        AccountPruneFilter{h.rt.clock().system_now() + 1h, std::nullopt}, DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    const std::optional<ErasedOutcome> failed = h.outcome(wrong);
    REQUIRE(failed);
    const auto* failure = std::get_if<Failed>(&*failed);
    REQUIRE(failure != nullptr);
    CHECK(failure->error.id == "internal.bug");
}

TEST_CASE("purge is refused under a session and registers the identity again", "[backend][accounts]") {
    AccountsHarness h;
    h.accounts->register_identity(kIdentity);
    REQUIRE(h.service->start_backend(true, DisconnectPolicy::Detached));
    {
        BackendLease lease = h.service->acquire(session_id(1)).value();
        const Result<OpHandle> refused = h.accounts->start_purge(DisconnectPolicy::Detached);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().id == "backend.in_use");
    }
    h.rt.run_until_idle();
    const Result<OpHandle> purged = h.accounts->start_purge(DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    CHECK(h.succeeded(purged));
    CHECK(h.ids() == std::vector<std::string>{"Alpha-aaaaa1", "Beta-aaaaa2"});
}

TEST_CASE("account ops need the embedded backend", "[backend][accounts]") {
    AccountsHarness h(BackendConfig{BackendTarget{LocalBackend{}}, false});
    const Result<OpHandle> reset = h.accounts->start_reset("Alpha-aaaaa1", DisconnectPolicy::Detached);
    REQUIRE_FALSE(reset);
    CHECK(reset.error().id == "backend.embedded_only");
}

TEST_CASE("cancelling an account op releases its lease", "[backend][accounts]") {
    AccountsHarness h;
    h.script.ready_delay = 10s;
    const Result<OpHandle> reset = h.accounts->start_reset("Alpha-aaaaa1", DisconnectPolicy::Detached);
    REQUIRE(reset);
    h.rt.run_until_idle();
    CHECK(h.service->state().leases == 1);
    REQUIRE(h.rt.ops().cancel(reset->id(), CancelReason::User));
    h.rt.run_until_idle();
    const std::optional<ErasedOutcome> cancelled = h.outcome(reset);
    REQUIRE(cancelled);
    CHECK(std::holds_alternative<Cancelled>(*cancelled));
    CHECK(h.service->state().leases == 0);
    CHECK(h.service->state().phase == BackendPhase::Stopped);
}

TEST_CASE("purge is refused when a session leased the backend while it was starting", "[backend][accounts]") {
    AccountsHarness h;
    h.script.ready_delay = 1s;
    h.accounts->register_identity(kIdentity);
    const Result<OpHandle> purge = h.accounts->start_purge(DisconnectPolicy::Detached);
    REQUIRE(purge);
    h.rt.run_until_idle();
    Result<BackendLease> lease = h.service->acquire(session_id(1));
    REQUIRE(lease);
    h.rt.advance(1s);
    const std::optional<ErasedOutcome> refused = h.outcome(purge);
    REQUIRE(refused);
    const auto* failure = std::get_if<Failed>(&*refused);
    REQUIRE(failure != nullptr);
    CHECK(failure->error.id == "backend.in_use");
    CHECK(h.ids() == std::vector<std::string>{"Alpha-aaaaa1", "Beta-aaaaa2"});
}

TEST_CASE("account ops end when the accounts service goes away", "[backend][accounts]") {
    AccountsHarness h;
    h.script.child.reply_delay = 1s;
    const Result<OpHandle> waiting = h.accounts->start_reset("Alpha-aaaaa1", DisconnectPolicy::Detached);
    REQUIRE(waiting);
    h.rt.run_until_idle();
    CHECK(h.service->state().leases == 1);

    SECTION("while waiting for the backend") {}
    SECTION("while the backend answers") {
        // Ready, then the health check; the reset itself is now in flight.
        h.rt.advance(1s);
        REQUIRE(h.service->state().phase == BackendPhase::Running);
    }
    h.accounts.reset();
    h.rt.advance(5s);
    const std::optional<ErasedOutcome> ended = h.outcome(waiting);
    REQUIRE(ended);
    const auto* cancelled = std::get_if<Cancelled>(&*ended);
    REQUIRE(cancelled != nullptr);
    CHECK(cancelled->reason == CancelReason::Shutdown);
    CHECK(h.service->state().leases == 0);
}
