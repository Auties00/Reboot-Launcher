#pragma once

#include <memory>
#include <optional>
#include <string>

#include "reboot/backend/backend_info.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/secrets/secret_service.hpp"

namespace reboot {
class Redactor;
}

namespace reboot::net {
class HttpClient;
}

namespace reboot::backend {

struct RemoteLoginRequest {
    BackendInfo upstream;
    std::string login;
    // The build takes -AUTH_TYPE=exchangecode; honoured only by a Reboot upstream.
    bool want_exchange_code = false;
    // Who waits when the password is missing or rejected.
    secrets::SecretWait wait;
    // Receives each NeedsSecret id, for the op's awaiting_user().
    UniqueFunction<void(RequestId)> awaiting_user;
};

struct RemoteLoginResult {
    // Single use; registered with the Redactor.
    std::optional<SecretString> exchange_code;
};

// Capabilities: none assigned (decisions credential-security, backend-architecture).
// Strand-only. Logs in before the game starts, so a wrong password surfaces as NeedsSecret here:
// - Reboot upstream: a password grant, then /account/api/oauth/exchange when a code is wanted;
// - any other upstream: the password grant alone, checking the password the front later swaps in.
// The password is the stored RemoteBackendPassword for the upstream host; a missing or rejected
// one raises NeedsSecret through SecretService::require and the login is retried with the answer.
class RemoteLogin {
public:
    RemoteLogin(net::HttpClient& http, secrets::SecretService& secrets, Redactor& redactor);
    ~RemoteLogin();
    RemoteLogin(const RemoteLogin&) = delete;
    RemoteLogin& operator=(const RemoteLogin&) = delete;

    // `done` runs on the strand exactly once; cancelling `token` withdraws a pending NeedsSecret.
    void login(RemoteLoginRequest request, CancelToken token, UniqueFunction<void(Result<RemoteLoginResult>)> done);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::backend
