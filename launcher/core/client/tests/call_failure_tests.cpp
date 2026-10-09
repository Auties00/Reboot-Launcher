#include <catch2/catch_test_macros.hpp>

#include "call_failure.hpp"
#include "messages.hpp"
#include "reboot/client.h"
#include "reboot/ipc/ipc_errors.hpp"

using namespace rb;
using namespace rb::client;

namespace {

[[nodiscard]] rb_status local(MessageId id, ErrorKind kind = ErrorKind::Generic) {
    return status_for(local_failure(make_diag(ErrorDomain::Ipc, id).kind(kind).build()));
}

[[nodiscard]] CallFailure remote(const char* id) {
    CallFailure failure;
    failure.diagnostic.id = id;
    failure.remote = true;
    return failure;
}

}  // namespace

TEST_CASE("local failures map to the status reboot/client.h documents for their id", "[client][status]") {
    CHECK(local(msg::kInvalidArgument) == RB_E_INVALID_ARG);
    CHECK(local(ipc::kUnknownOp) == RB_E_INVALID_ARG);
    CHECK(local(ipc::kUnknownSubscription) == RB_E_INVALID_ARG);
    CHECK(local(msg::kAbiMismatch) == RB_E_ABI_MISMATCH);
    CHECK(local(msg::kCallTimedOut) == RB_E_TIMEOUT);
    CHECK(local(msg::kClosed) == RB_E_CLOSED);
    CHECK(local(ipc::kTooManyCalls) == RB_E_LIMIT);
    CHECK(local(ipc::kTooManySubscriptions) == RB_E_LIMIT);
    CHECK(local(ipc::kEngineUnavailable) == RB_E_ENGINE_UNAVAILABLE);
    CHECK(local(ipc::kUpdateInProgress) == RB_E_ENGINE_UNAVAILABLE);
    CHECK(local(ipc::kEndpointUntrusted) == RB_E_ENGINE_ENDPOINT_UNTRUSTED);
    CHECK(local(ipc::kEngineCannotDetach) == RB_E_ENGINE_CANNOT_DETACH);
    CHECK(local(ipc::kVersionMismatch) == RB_E_ENGINE_VERSION_MISMATCH);
    CHECK(local(ipc::kRootMismatch) == RB_E_ENGINE_ROOT_MISMATCH);
    CHECK(local(ipc::kElevatedAutostartRefused) == RB_E_ELEVATED_AUTOSTART_REFUSED);
    CHECK(local(ipc::kNoInteractiveSession) == RB_E_NO_INTERACTIVE_SESSION);
    CHECK(local(ipc::kAgentRequiresApproval) == RB_E_AGENT_REQUIRES_APPROVAL);
    CHECK(local(ipc::kConnectionLost) == RB_E_CONNECTION_LOST);
    CHECK(local(ipc::kEngineClosed) == RB_E_CONNECTION_LOST);
}

TEST_CASE("an id the table does not know maps by its ErrorKind, else to RB_E_INTERNAL", "[client][status]") {
    const MessageId adapter_failure{"os_linux.connect_refused"};
    CHECK(local(adapter_failure, ErrorKind::EngineUnavailable) == RB_E_ENGINE_UNAVAILABLE);
    CHECK(local(adapter_failure, ErrorKind::InvalidInput) == RB_E_INVALID_ARG);
    CHECK(local(adapter_failure, ErrorKind::Generic) == RB_E_INTERNAL);
}

TEST_CASE("a remote failure is RB_E_REMOTE, and play's session refusal only matters to rb_start", "[client][status]") {
    CHECK(status_for(remote("library.not_found")) == RB_E_REMOTE);
    CHECK(status_for(remote("play.wrong_session")) == RB_E_REMOTE);
    CHECK(start_status_for(remote("play.wrong_session")) == RB_E_ENGINE_OTHER_SESSION);
    CHECK(start_status_for(remote("library.not_found")) == RB_E_REMOTE);

    // The same id raised locally is not the engine's refusal.
    CallFailure local_wrong_session = remote("play.wrong_session");
    local_wrong_session.remote = false;
    CHECK(start_status_for(local_wrong_session) == RB_E_INTERNAL);
}
