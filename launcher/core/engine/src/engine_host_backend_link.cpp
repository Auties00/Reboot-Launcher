#include "engine_host_backend_link.hpp"

#include <algorithm>
#include <memory>

#include "messages.hpp"
#include "reboot/backend/backend_service.hpp"
#include "reboot/backend/backend_session_config.hpp"
#include "reboot/backend/backend_upstream.hpp"
#include "reboot/backend/launch_credential.hpp"
#include "reboot/contracts/backend.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/foundation/secret.hpp"

namespace reboot::engine {

namespace {

constexpr std::size_t kSessionKeyBytes = 16;

[[nodiscard]] Diagnostic cancelled() {
    return make_diag(ErrorDomain::Engine, msg::kCancelled).arg("what", "backend lease").kind(ErrorKind::Cancelled);
}

}  // namespace

backend::BackendLease* EngineHostBackendLink::lease_of(const SessionId& session) {
    const auto it = std::ranges::find(leases_, session, &std::pair<SessionId, backend::BackendLease>::first);
    return it == leases_.end() ? nullptr : &it->second;
}

void EngineHostBackendLink::acquire(SessionId session, std::string account_id, CancelToken token,
                                    UniqueFunction<void(Result<gameserver::BackendAccess>)> done) {
    auto finish = std::make_shared<UniqueFunction<void(Result<gameserver::BackendAccess>)>>(std::move(done));
    auto fail = [this, session, finish](Diagnostic error) {
        release(session);
        (*finish)(std::unexpected(std::move(error)));
    };

    Result<backend::BackendLease> lease = backend_.acquire(session);
    if (!lease) {
        strand_.post([finish, error = std::move(lease.error())]() mutable { (*finish)(std::unexpected(std::move(error))); });
        return;
    }
    release(session);
    leases_.emplace_back(session, std::move(*lease));

    backend_.ensure_ready(
        *lease_of(session), token,
        [this, session, account_id = std::move(account_id), token, finish, fail](
            Result<backend::BackendUpstream> upstream) mutable {
            if (!upstream) return fail(std::move(upstream.error()));
            backend::BackendLease* held = lease_of(session);
            if (held == nullptr || token.cancelled()) return fail(cancelled());

            backend::BackendSessionConfig config;
            config.session_key = SecretString{random_token_hex(random_, kSessionKeyBytes)};
            config.account_id = account_id;
            config.origin = SecretString{upstream->origin};
            std::string origin = upstream->origin;
            backend_.configure_session(
                *held, std::move(config),
                [this, session, account_id, origin = std::move(origin), token, finish, fail](Result<void> configured) mutable {
                    if (!configured) return fail(std::move(configured.error()));
                    backend::BackendLease* current = lease_of(session);
                    if (current == nullptr || token.cancelled()) return fail(cancelled());
                    backend::LaunchCredentialRequest request;
                    request.account_id = account_id;
                    request.kind = contracts::backend::CredentialKind::LaunchSecret;
                    backend_.mint_launch_credential(
                        *current, std::move(request),
                        [this, session, account_id, origin = std::move(origin), token, finish, fail](
                            Result<backend::LaunchCredential> credential) mutable {
                            if (!credential) return fail(std::move(credential.error()));
                            if (lease_of(session) == nullptr || token.cancelled()) return fail(cancelled());
                            (*finish)(gameserver::BackendAccess{std::move(origin), std::move(account_id),
                                                                std::move(credential->value)});
                        });
                });
        });
}

void EngineHostBackendLink::release(SessionId session) {
    const auto it = std::ranges::find(leases_, session, &std::pair<SessionId, backend::BackendLease>::first);
    if (it == leases_.end()) return;
    backend::BackendLease lease = std::move(it->second);
    leases_.erase(it);
    lease.release();
}

}  // namespace reboot::engine
