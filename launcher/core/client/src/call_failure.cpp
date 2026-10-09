#include "call_failure.hpp"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

#include "messages.hpp"
#include "reboot/ipc/ipc_errors.hpp"

namespace rb::client {

namespace {

// play's refusal of a caller in another OS session; the library links no domain package.
constexpr std::string_view kPlayWrongSession = "play.wrong_session";

struct StatusById {
    const MessageId* id;
    rb_status status;
};

[[nodiscard]] std::optional<rb_status> status_by_id(std::string_view id) noexcept {
    static const std::array<StatusById, 21> table{{
        {&msg::kInvalidArgument, RB_E_INVALID_ARG},
        {&ipc::kUnknownOp, RB_E_INVALID_ARG},
        {&ipc::kUnknownSubscription, RB_E_INVALID_ARG},
        {&msg::kAbiMismatch, RB_E_ABI_MISMATCH},
        {&msg::kCallTimedOut, RB_E_TIMEOUT},
        {&msg::kClosed, RB_E_CLOSED},
        {&ipc::kTooManyCalls, RB_E_LIMIT},
        {&ipc::kTooManySubscriptions, RB_E_LIMIT},
        {&ipc::kEngineUnavailable, RB_E_ENGINE_UNAVAILABLE},
        {&ipc::kUpdateInProgress, RB_E_ENGINE_UNAVAILABLE},
        {&ipc::kEndpointUntrusted, RB_E_ENGINE_ENDPOINT_UNTRUSTED},
        {&ipc::kEngineCannotDetach, RB_E_ENGINE_CANNOT_DETACH},
        {&ipc::kVersionMismatch, RB_E_ENGINE_VERSION_MISMATCH},
        {&ipc::kRootMismatch, RB_E_ENGINE_ROOT_MISMATCH},
        {&ipc::kElevatedAutostartRefused, RB_E_ELEVATED_AUTOSTART_REFUSED},
        {&ipc::kNoInteractiveSession, RB_E_NO_INTERACTIVE_SESSION},
        {&ipc::kAgentRequiresApproval, RB_E_AGENT_REQUIRES_APPROVAL},
        {&ipc::kConnectionLost, RB_E_CONNECTION_LOST},
        {&ipc::kEngineClosed, RB_E_CONNECTION_LOST},
        {&ipc::kInvalidEndpointInput, RB_E_INVALID_ARG},
        {&ipc::kProtocolError, RB_E_CONNECTION_LOST},
    }};
    for (const auto& entry : table)
        if (entry.id->id == id) return entry.status;
    return std::nullopt;
}

}  // namespace

CallFailure local_failure(const Diagnostic& diagnostic) { return CallFailure{contracts::common::to_wire(diagnostic)}; }

rb_status status_for(const CallFailure& failure) noexcept {
    if (failure.remote) return RB_E_REMOTE;
    if (const auto status = status_by_id(failure.diagnostic.id)) return *status;
    switch (failure.diagnostic.kind) {
        case ErrorKind::EngineUnavailable: return RB_E_ENGINE_UNAVAILABLE;
        case ErrorKind::InvalidInput:
        case ErrorKind::NotFound: return RB_E_INVALID_ARG;
        default: return RB_E_INTERNAL;
    }
}

rb_status start_status_for(const CallFailure& failure) noexcept {
    if (failure.remote && failure.diagnostic.id == kPlayWrongSession) return RB_E_ENGINE_OTHER_SESSION;
    return status_for(failure);
}

}  // namespace rb::client
