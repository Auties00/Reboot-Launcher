#pragma once

#include <any>
#include <memory>
#include <optional>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace rb {

class EventBus;

enum class UserRequestKind : u8 {
    NeedsSecret,
    ConfirmJoin,
    NeedsJoinPassword,
    AutoServerConsent,
    ConfirmUnencryptedUpstream,
    AccountRenameConflict,
    RosettaInstall,
    AgentRequiresApproval,
    ChooseVersion,
    ConfirmUntested,
    ConfirmStopSessions,
};

// `payload` is a domain struct defined by the package that raises the request.
struct UserRequest {
    RequestId id;
    UserRequestKind kind{};
    std::any payload;
    std::optional<OpId> op;
    std::optional<SessionId> session;
};

enum class RequestResolution : u8 { Answered, Withdrawn };

struct UserActionResolvedEvent {
    RequestId id;
    RequestResolution resolution{};
};

// Strand-only; the owning tokens are cancelled on the strand. Publishes the UserRequest itself
// as UserActionRequired, and UserActionResolvedEvent.
// Covers onboarding-ux-flows.decision-dialogs: each decision a dialog made is now a request.
class UserRequestRegistry {
public:
    explicit UserRequestRegistry(EventBus& events);
    ~UserRequestRegistry();
    UserRequestRegistry(const UserRequestRegistry&) = delete;
    UserRequestRegistry& operator=(const UserRequestRegistry&) = delete;

    // `on_answer` validates the answer; an error leaves the request pending. Cancelling
    // `token` withdraws the request.
    RequestId ask(UserRequestKind kind, std::any payload, std::optional<OpId> op, std::optional<SessionId> session,
                  UniqueFunction<Result<void>(const std::any& answer)> on_answer, CancelToken token);

    // The first valid answer wins; later ones get requests.already_resolved.
    Result<void> respond(RequestId id, std::any answer);

    [[nodiscard]] std::vector<UserRequest> pending() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb
