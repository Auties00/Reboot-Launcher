#pragma once

#include <memory>
#include <string>
#include <vector>

#include "reboot/backend/account_prune_filter.hpp"
#include "reboot/backend/account_rename_request.hpp"
#include "reboot/backend/backend_account.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/identity/identity_changed_event.hpp"
#include "reboot/identity/identity_service.hpp"

namespace rb {
class EventBus;
class UserRequestRegistry;
}  // namespace rb

namespace rb::backend {

class BackendProcess;
class BackendService;

// Capabilities: none assigned (decisions backend-state-per-user, identity-uniqueness).
// Strand-only. The embedded backend's accounts and data. Every op takes a maintenance lease, so it
// starts the backend if needed and fails with backend.embedded_only for Local and Remote.
class BackendAccounts {
public:
    BackendAccounts(BackendService& service, BackendProcess& process, OpRegistry& ops, UserRequestRegistry& requests,
                    EventBus& events);
    ~BackendAccounts();
    BackendAccounts(const BackendAccounts&) = delete;
    BackendAccounts& operator=(const BackendAccounts&) = delete;

    // Engine startup, after IdentityService::ensure_records().
    void register_identity(const identity::IdentitySnapshot& identity);
    // EventKind::IdentityChanged: a new account_id for a known record_id renames the backend data
    // from the id last registered for it, then re-registers the record.
    void on_identity_changed(const identity::IdentityChangedEvent& event);

    // As of the last Running or account op, each of which publishes BackendAccountsChanged;
    // backend.accounts_not_loaded before the first.
    [[nodiscard]] Result<std::vector<BackendAccount>> list() const;

    Result<OpHandle> start_reset(std::string account_id, DisconnectPolicy policy);
    Result<OpHandle> start_delete(std::string account_id, DisconnectPolicy policy);
    // Completes with the number of accounts removed. Every removed account the backend reports is
    // checked with AccountPruneFilter::selects; one it would not select fails the op with internal.bug.
    Result<OpHandle> start_prune(AccountPruneFilter filter, DisconnectPolicy policy);
    // Completes with the new account id.
    Result<OpHandle> start_rename(AccountRenameRequest rename, DisconnectPolicy policy);
    // Fails with backend.in_use while a session lease is live.
    Result<OpHandle> start_purge(DisconnectPolicy policy);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::backend
