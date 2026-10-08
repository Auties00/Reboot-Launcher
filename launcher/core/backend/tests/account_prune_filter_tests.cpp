#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>

#include "reboot/backend/account_prune_filter.hpp"

using namespace reboot;
using namespace reboot::backend;

namespace {

constexpr std::chrono::system_clock::time_point kCutoff{std::chrono::hours{1000}};

BackendAccount stale_lan_account() {
    BackendAccount account;
    account.account_id = "friend";
    account.kind = BackendAccountKind::Remote;
    account.last_login = kCutoff - std::chrono::hours{1};
    return account;
}

}  // namespace

TEST_CASE("a stale LAN account is pruned", "[backend]") {
    CHECK(AccountPruneFilter{kCutoff, std::nullopt}.selects(stale_lan_account()));
}

TEST_CASE("our own accounts are never pruned", "[backend]") {
    const AccountPruneFilter filter{kCutoff, std::nullopt};
    BackendAccount local = stale_lan_account();
    local.kind = BackendAccountKind::Local;
    CHECK_FALSE(filter.selects(local));

    BackendAccount recorded = stale_lan_account();
    recorded.record = AccountRecordId{};
    CHECK_FALSE(filter.selects(recorded));
}

TEST_CASE("a recent or never seen account is kept", "[backend]") {
    const AccountPruneFilter filter{kCutoff, std::nullopt};
    BackendAccount recent = stale_lan_account();
    recent.last_login = kCutoff;
    CHECK_FALSE(filter.selects(recent));

    BackendAccount unseen = stale_lan_account();
    unseen.last_login.reset();
    CHECK_FALSE(filter.selects(unseen));
}

TEST_CASE("a role narrows the prune", "[backend]") {
    BackendAccount host = stale_lan_account();
    host.role = AccountRole::Host;
    CHECK_FALSE(AccountPruneFilter{kCutoff, AccountRole::Client}.selects(host));
    CHECK(AccountPruneFilter{kCutoff, AccountRole::Host}.selects(host));
}
