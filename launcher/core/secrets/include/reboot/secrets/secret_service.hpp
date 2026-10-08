#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string_view>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/secrets/needs_secret.hpp"
#include "reboot/secrets/secret_kind.hpp"
#include "reboot/secrets/secret_state.hpp"
#include "reboot/secrets/secret_target.hpp"
#include "reboot/secrets/secrets_availability.hpp"

namespace reboot {
class EventBus;
class Executor;
class Redactor;
class TimerService;
class UserRequestRegistry;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class ISecretStore;
}

namespace reboot::secrets {

// A blocked store call (a libsecret or Keychain prompt) is abandoned after this; its late
// result is discarded.
inline constexpr std::chrono::seconds kStoreCallDeadline{10};

// Who is waiting for a secret raised through require().
struct SecretWait {
    std::optional<SessionId> session;
    std::optional<OpId> op;
    NeedsSecretReason reason = NeedsSecretReason::Missing;
};

// Capabilities: none assigned (decisions credential-security, shared-store-cli-gui, logging-redaction).
// Strand-only. The engine's only holder of secrets; no value is ever logged.
// - Every held value is registered with the Redactor. A dropped value (cleared, replaced, taken,
//   or its request resolved) is removed once a Logger::flush on the WorkerPool has written the
//   records queued before the drop. Values under 4 bytes are never masked (logging-redaction).
// - Store calls run on the WorkerPool under kStoreCallDeadline; writes for one target apply in
//   call order.
// - Every change of a target's SecretState publishes a SecretStateChangedEvent.
// - Remember writes wherever the store's kind() says: the OS store, or its owner-only file
//   fallback. With no store (macOS outside Aqua) or a failed load, secrets are session-only.
class SecretService {
public:
    // `root_hash16` namespaces the store keys, so engines on different data roots never share them.
    SecretService(ports::ISecretStore& store, WorkerPool& workers, Executor& strand, TimerService& timers,
                  UserRequestRegistry& requests, EventBus& events, Redactor& redactor, std::string_view root_hash16);
    ~SecretService();
    SecretService(const SecretService&) = delete;
    SecretService& operator=(const SecretService&) = delete;

    // Loads every stored secret, so the queries below never block. A store that is unavailable,
    // fails or times out leaves the service usable with session-only secrets; `done` runs on the
    // strand with the resulting availability.
    void start(UniqueFunction<void(SecretsAvailability)> done);

    [[nodiscard]] SecretsAvailability availability() const;

    // From IPC SecretPut; the value is never read back over IPC except through
    // reveal(). It is held at once, also while start() is loading: its store write then waits for
    // the load, and a loaded copy never replaces it. An empty `retention` is default_retention().
    // Remember writes the value to the store and Session erases any stored copy; `saved` (may be
    // empty) runs on the strand when that ends. A failed Remember write keeps the value held with
    // SecretState::not_saved, so a caller deleting another copy waits for success.
    // Fails synchronously with secrets.empty_value, secrets.too_large, secrets.retention_not_allowed,
    // or secrets.request_not_pending for a JoinPassword whose NeedsJoinPassword is not pending.
    Result<void> put(const SecretTarget& target, SecretBytes value, std::optional<Retention> retention,
                     UniqueFunction<void(Result<SecretState>)> saved);

    // Fails only with secrets.not_ready, until start() has loaded.
    [[nodiscard]] Result<SecretState> state(const SecretTarget& target) const;

    // Drops the held value now; `done` runs on the strand once the stored copy is erased
    // (secrets.store_erase_failed, secrets.store_timed_out).
    void clear(const SecretTarget& target, UniqueFunction<void(Result<void>)> done);

    // For engine services only. A copy, so it can travel to a worker; secrets.not_found when absent.
    [[nodiscard]] Result<SecretBytes> provide(const SecretTarget& target) const;

    // JoinPassword only, for browser's JoinPasswordSource: hands the value over and drops it at once;
    // secrets.not_found when absent. One never taken is dropped when its request resolves.
    [[nodiscard]] Result<SecretBytes> take(const SecretTarget& target);

    // For a client; secrets.reveal_forbidden for every kind but HostJoinPassword.
    [[nodiscard]] Result<SecretBytes> reveal(const SecretTarget& target) const;

    // With reason Missing and a held value, `done` gets it (posted) and nullopt is returned.
    // Otherwise raises NeedsSecret and returns its id, for the op's awaiting_user(); the answer
    // is accepted only after a value newer than the request was put. Cancelling `token`
    // withdraws the request and `done` gets secrets.request_withdrawn.
    std::optional<RequestId> require(const SecretTarget& target, SecretWait wait, CancelToken token,
                                     UniqueFunction<void(Result<SecretBytes>)> done);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::secrets
