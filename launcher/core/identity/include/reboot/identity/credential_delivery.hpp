#pragma once

#include <variant>

#include "reboot/contracts/backend.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::identity {

// The embedded backend mints a single-use, 5-minute value bound to the build (MintLaunchCredential).
struct BackendMinted {
    contracts::backend::CredentialKind kind{};

    bool operator==(const BackendMinted&) const = default;
};

// The engine runs the password grant upstream with the stored secret, then hands the game an
// exchange code.
struct RemotePasswordExchange {
    bool operator==(const RemotePasswordExchange&) const = default;
};

// PassThrough forwards the ticket itself to an upstream that checks no password.
enum class TicketRedemption : u8 { SwapForStoredPassword, PassThrough };

// The game gets a per-session random ticket; the front redeems it on POST /account/api/oauth/token.
struct FrontTicket {
    TicketRedemption redemption{};

    bool operator==(const FrontTicket&) const = default;
};

// The real password in argv, for a custom auth DLL that talks to a hosted backend directly.
struct LegacyArgv {
    bool operator==(const LegacyArgv&) const = default;
};

using CredentialDelivery = std::variant<BackendMinted, RemotePasswordExchange, FrontTicket, LegacyArgv>;

// True when the launch needs the account's stored password; the engine raises NeedsSecret if absent.
[[nodiscard]] constexpr bool needs_stored_password(const CredentialDelivery& delivery) noexcept {
    if (std::holds_alternative<FrontTicket>(delivery))
        return std::get<FrontTicket>(delivery).redemption == TicketRedemption::SwapForStoredPassword;
    return std::holds_alternative<RemotePasswordExchange>(delivery) || std::holds_alternative<LegacyArgv>(delivery);
}

}  // namespace reboot::identity
