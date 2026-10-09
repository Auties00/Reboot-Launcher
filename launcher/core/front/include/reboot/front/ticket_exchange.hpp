#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/front/peer_user.hpp"

namespace rb::front {

inline constexpr std::string_view kOauthTokenPath = "/account/api/oauth/token";

// A Refused swap gets Epic's invalid-credentials error, which the game shows as a failed login.
inline constexpr u32 kRefusedGrantStatus = 400;
inline constexpr std::string_view kRefusedGrantErrorName = "errors.com.epicgames.account.invalid_account_credentials";
inline constexpr std::string_view kRefusedGrantErrorCode = "18031";
inline constexpr std::string_view kRefusedGrantBody =
    R"({"errorCode":"errors.com.epicgames.account.invalid_account_credentials",)"
    R"("errorMessage":"Sorry the account credentials you are using are invalid","messageVars":[],)"
    R"("numericErrorCode":18031,"originatingService":"com.epicgames.account.public","intent":"prod",)"
    R"("error_description":"Sorry the account credentials you are using are invalid","error":"invalid_grant"})";

// Bound: our DLL and the /s/<key>/ prefix, so the game may log in again. Unbound: LegacyFixed, one login.
enum class TicketBinding : u8 { Bound, Unbound };

// Reserved: an Unbound ticket whose swapped grant is still upstream; only Unbound tickets leave Available.
enum class TicketState : u8 { Available, Reserved, Spent };

// The stored login of a password-backed upstream account.
struct UpstreamLogin {
    std::string username;
    SecretString password;
};

// PassedThrough: the body carries no ticket (refresh_token, exchange_code, another password).
enum class TicketSwapOutcome : u8 { Swapped, PassedThrough, Refused };

struct TicketSwap {
    TicketSwapOutcome outcome = TicketSwapOutcome::PassedThrough;
    // The re-encoded form; set only when Swapped.
    SecretBytes body;
};

// Capabilities: auth-backend.reverse-proxy, auth-backend.+12.
// Swaps the game's -AUTH_PASSWORD=<ticket> for the stored login, the one body the front rewrites. Strand-only.
class TicketExchange {
public:
    TicketExchange(SecretString ticket, UpstreamLogin login, TicketBinding binding);

    [[nodiscard]] static bool applies(std::string_view method, std::string_view path) noexcept;

    // `form` is the urlencoded body. Refused for an Other peer, or an Unbound ticket that is not Available.
    [[nodiscard]] TicketSwap swap(std::span<const u8> form, PeerUser peer);

    // Once per Swapped grant: a 2xx spends a Reserved ticket; another status, or none, frees it.
    void settle(std::optional<u32> upstream_status) noexcept;

    [[nodiscard]] TicketState state() const noexcept { return state_; }

private:
    SecretString ticket_;
    UpstreamLogin login_;
    TicketBinding binding_;
    TicketState state_ = TicketState::Available;
};

}  // namespace rb::front
