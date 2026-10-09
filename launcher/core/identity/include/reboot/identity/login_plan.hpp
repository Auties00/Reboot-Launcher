#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/identity/account_record.hpp"
#include "reboot/identity/credential_delivery.hpp"
#include "reboot/identity/login_target.hpp"

namespace rb::identity {

enum class AuthType : u8 { Epic, ExchangeCode };

// The -AUTH_TYPE value.
[[nodiscard]] constexpr std::string_view auth_type_value(AuthType type) noexcept {
    return type == AuthType::ExchangeCode ? "exchangecode" : "epic";
}

// Holds no secret: the engine fills -AUTH_PASSWORD from `delivery`, never with a fixed literal.
struct LoginPlan {
    // The -AUTH_LOGIN value, effective_login.
    std::string auth_login;
    AuthType auth_type{};
    CredentialDelivery delivery;
    // identity.password_in_argv for LegacyArgv.
    std::vector<Diagnostic> warnings;
};

// Capabilities: profile-identity.+47.
// The login `record` really sends on `target`, so every UI labels it the same way. The host never
// runs the game: its login is the account id the game server gets through ServerConfig.
// - Embedded: <account_id>@projectreboot.dev.
// - With a remote_login: that login.
// - Reboot upstream without: <account_id>@projectreboot.dev.
// - ThirdParty without: third_party_login(display_name).
[[nodiscard]] std::string effective_login(const AccountRecord& record, const LoginTarget& target);

// Capabilities: profile-identity.credentials, profile-identity.+47, auth-backend.lawinserver-xmpp.
// Play only, so `record` is the client's; the host's is an internal.bug. Every build takes epic, so
// it is the fallback wherever exchangecode cannot be used.
// - Embedded: exchangecode when the build takes it, otherwise epic; BackendMinted.
// - Reboot upstream with remote_login: exchangecode builds get RemotePasswordExchange, others epic
//   with FrontTicket{SwapForStoredPassword}.
// - Reboot upstream without: epic, FrontTicket{PassThrough}.
// - ThirdParty: epic, FrontTicket that swaps only when there is a remote_login.
// - LegacyArgv: epic, LegacyArgv. Needs a Local or Remote backend, a custom auth DLL and a login.
// Fails with identity.empty_remote_login, identity.legacy_argv_needs_hosted_backend,
// identity.legacy_argv_needs_custom_auth_dll or identity.legacy_argv_needs_login.
[[nodiscard]] Result<LoginPlan> plan_login(const AccountRecord& record, const LoginTarget& target,
                                           bool build_takes_exchangecode);

}  // namespace rb::identity
