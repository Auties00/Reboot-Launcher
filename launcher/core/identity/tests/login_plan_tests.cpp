#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <utility>

#include "messages.hpp"
#include "reboot/identity/login_plan.hpp"

using namespace reboot;
using namespace reboot::identity;
using contracts::backend::CredentialKind;
using storage::BackendKind;

namespace {

const AccountRecord kClient{AccountRecordId{}, AccountRole::Client, "Bob", "a1b2c3"};
const AccountRecord kHost{AccountRecordId{}, AccountRole::Host, "Hoster", "z9y8x7"};

LoginTarget hosted(UpstreamFlavor flavor, std::optional<std::string> login = std::nullopt) {
    return LoginTarget{BackendKind::Remote, flavor, std::move(login), CredentialPolicy::Ticket, false};
}

LoginTarget legacy_argv(BackendKind backend, std::optional<std::string> login, bool custom_auth_dll) {
    return LoginTarget{backend, UpstreamFlavor::ThirdParty, std::move(login), CredentialPolicy::LegacyArgv,
                       custom_auth_dll};
}

LoginPlan plan(const LoginTarget& target, bool exchangecode) {
    auto result = plan_login(kClient, target, exchangecode);
    REQUIRE(result);
    return std::move(*result);
}

}  // namespace

TEST_CASE("needs_stored_password follows the delivery", "[identity]") {
    STATIC_REQUIRE_FALSE(needs_stored_password(BackendMinted{}));
    STATIC_REQUIRE(needs_stored_password(RemotePasswordExchange{}));
    STATIC_REQUIRE(needs_stored_password(FrontTicket{TicketRedemption::SwapForStoredPassword}));
    STATIC_REQUIRE_FALSE(needs_stored_password(FrontTicket{TicketRedemption::PassThrough}));
    STATIC_REQUIRE(needs_stored_password(LegacyArgv{}));
}

TEST_CASE("effective_login is what each target sends", "[identity]") {
    CHECK(effective_login(kClient, LoginTarget{}) == "Bob-a1b2c3@projectreboot.dev");
    CHECK(effective_login(kClient, hosted(UpstreamFlavor::Reboot)) == "Bob-a1b2c3@projectreboot.dev");
    CHECK(effective_login(kClient, hosted(UpstreamFlavor::ThirdParty)) == "Bob@projectreboot.dev");
    CHECK(effective_login(kClient, hosted(UpstreamFlavor::Reboot, "bob@example.com")) == "bob@example.com");
    CHECK(effective_login(kClient, hosted(UpstreamFlavor::ThirdParty, "bob@example.com")) == "bob@example.com");
    CHECK(effective_login(kHost, hosted(UpstreamFlavor::ThirdParty, "bob@example.com")) == "Hoster-z9y8x7");
}

TEST_CASE("plan_login on the embedded backend mints a credential", "[identity]") {
    const LoginPlan epic = plan(LoginTarget{}, false);
    CHECK(epic.auth_login == "Bob-a1b2c3@projectreboot.dev");
    CHECK(epic.auth_type == AuthType::Epic);
    CHECK(epic.delivery == CredentialDelivery{BackendMinted{CredentialKind::LaunchSecret}});

    const LoginPlan exchange = plan(LoginTarget{}, true);
    CHECK(exchange.auth_type == AuthType::ExchangeCode);
    CHECK(exchange.delivery == CredentialDelivery{BackendMinted{CredentialKind::ExchangeCode}});
    CHECK(exchange.warnings.empty());
}

TEST_CASE("plan_login on a Reboot upstream", "[identity]") {
    const LoginPlan with_login_exchange = plan(hosted(UpstreamFlavor::Reboot, "bob@example.com"), true);
    CHECK(with_login_exchange.auth_type == AuthType::ExchangeCode);
    CHECK(with_login_exchange.delivery == CredentialDelivery{RemotePasswordExchange{}});

    const LoginPlan with_login_epic = plan(hosted(UpstreamFlavor::Reboot, "bob@example.com"), false);
    CHECK(with_login_epic.auth_type == AuthType::Epic);
    CHECK(with_login_epic.delivery == CredentialDelivery{FrontTicket{TicketRedemption::SwapForStoredPassword}});

    for (const bool exchangecode : {false, true}) {
        const LoginPlan without_login = plan(hosted(UpstreamFlavor::Reboot), exchangecode);
        CHECK(without_login.auth_login == "Bob-a1b2c3@projectreboot.dev");
        CHECK(without_login.auth_type == AuthType::Epic);
        CHECK(without_login.delivery == CredentialDelivery{FrontTicket{TicketRedemption::PassThrough}});
    }
}

TEST_CASE("plan_login on a third-party upstream always sends epic", "[identity]") {
    for (const bool exchangecode : {false, true}) {
        const LoginPlan with_login = plan(hosted(UpstreamFlavor::ThirdParty, "bob@example.com"), exchangecode);
        CHECK(with_login.auth_login == "bob@example.com");
        CHECK(with_login.auth_type == AuthType::Epic);
        CHECK(with_login.delivery == CredentialDelivery{FrontTicket{TicketRedemption::SwapForStoredPassword}});

        const LoginPlan without_login = plan(hosted(UpstreamFlavor::ThirdParty), exchangecode);
        CHECK(without_login.auth_login == "Bob@projectreboot.dev");
        CHECK(without_login.auth_type == AuthType::Epic);
        CHECK(without_login.delivery == CredentialDelivery{FrontTicket{TicketRedemption::PassThrough}});
    }
}

TEST_CASE("plan_login puts the password in argv only on opt-in, with a warning", "[identity]") {
    const LoginPlan legacy = plan(legacy_argv(BackendKind::Local, "bob@example.com", true), true);
    CHECK(legacy.auth_login == "bob@example.com");
    CHECK(legacy.auth_type == AuthType::Epic);
    CHECK(legacy.delivery == CredentialDelivery{LegacyArgv{}});
    REQUIRE(legacy.warnings.size() == 1);
    CHECK(legacy.warnings.front().is(msg::kPasswordInArgv));
    CHECK(legacy.warnings.front().severity == Severity::Warning);
}

TEST_CASE("plan_login refuses targets it cannot serve", "[identity]") {
    const auto embedded = plan_login(kClient, legacy_argv(BackendKind::Embedded, "bob", true), false);
    REQUIRE_FALSE(embedded);
    CHECK(embedded.error().is(msg::kLegacyArgvNeedsHostedBackend));

    const auto no_dll = plan_login(kClient, legacy_argv(BackendKind::Remote, "bob", false), false);
    REQUIRE_FALSE(no_dll);
    CHECK(no_dll.error().is(msg::kLegacyArgvNeedsCustomAuthDll));

    const auto no_login = plan_login(kClient, legacy_argv(BackendKind::Remote, std::nullopt, true), false);
    REQUIRE_FALSE(no_login);
    CHECK(no_login.error().is(msg::kLegacyArgvNeedsLogin));

    const auto empty = plan_login(kClient, hosted(UpstreamFlavor::Reboot, ""), false);
    REQUIRE_FALSE(empty);
    CHECK(empty.error().is(msg::kEmptyRemoteLogin));

    const auto host = plan_login(kHost, LoginTarget{}, false);
    REQUIRE_FALSE(host);
    CHECK(host.error().domain == ErrorDomain::Internal);
}
