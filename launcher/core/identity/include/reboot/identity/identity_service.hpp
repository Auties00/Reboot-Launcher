#pragma once

#include <string>
#include <string_view>

#include "reboot/base/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/identity/account_record.hpp"
#include "reboot/identity/backend_login.hpp"
#include "reboot/identity/backend_logins_document.hpp"
#include "reboot/identity/identity_snapshot.hpp"
#include "reboot/identity/login_target.hpp"
#include "reboot/storage/accounts_document.hpp"
#include "reboot/storage/backend_target.hpp"
#include "reboot/storage/document_store.hpp"

namespace rb {
class EventBus;
class IRandom;
}  // namespace rb

namespace rb::identity {

// Capabilities: profile-identity.credentials, profile-identity.+47, auth-backend.lawinserver-xmpp.
// Strand-only. Owns data/accounts.json, with the one player name and one client and one host record
// in v1, and data/backend-logins.json.
// - ensure_records() mints a missing record, or one storage dropped for a bad record_id or role,
//   with a fresh tag, and a default name when none is stored; a record whose tag storage refused
//   keeps its record_id and gets a fresh tag. A ReadOnly store, which its LoadReport already
//   reported, refuses the write, so the records then live in memory only.
// - A rename is one atomic commit. It changes both account_ids, never a record_id or tag. Running
//   sessions keep the record they pinned, so it takes effect at the next launch.
// - A hand edit is adopted like an API change and the result written back: a changed
//   display_name is a rename; an invalid name or tag keeps the one in memory, as the API would
//   refuse it; a dropped record is the one in memory again. Its issues are logged.
// - Every change publishes IdentityChangedEvent.
// - Passwords are not here: secrets stores them and plan_login says which one a launch needs.
class IdentityService {
public:
    // Takes both stores' reload hooks, so a hand edit is re-validated here.
    IdentityService(storage::DocumentStore<storage::AccountsDocument>& accounts,
                    storage::DocumentStore<BackendLoginsDocument>& logins, IRandom& random, EventBus& events);
    ~IdentityService();
    IdentityService(const IdentityService&) = delete;
    IdentityService& operator=(const IdentityService&) = delete;

    // Engine startup, once both stores are loaded.
    void ensure_records();

    [[nodiscard]] const IdentitySnapshot& get() const noexcept { return current_; }
    [[nodiscard]] const AccountRecord& record(AccountRole role) const noexcept {
        return role == AccountRole::Host ? current_.host : current_.client;
    }

    // Renames both roles. Validates with validate_display_name; the same name is a no-op. A ReadOnly
    // store fails it with storage.read_only.
    Result<IdentitySnapshot> set_display_name(std::string_view name);
    // A new default name; the tags are kept.
    Result<IdentitySnapshot> reset();

    // The defaults when nothing is stored for `endpoint`.
    [[nodiscard]] BackendLogin backend_login(const HostPort& endpoint) const;
    // One commit; storing the defaults removes the entry, otherwise the stored entry's unknown
    // members are kept. Fails with identity.empty_remote_login, storage.invalid_host or
    // storage.read_only. Publishes the identity, as the client's effective_login may have changed.
    Result<BackendLogin> set_backend_login(BackendLogin login);
    // Play's input to plan_login, and with effective_login what every UI shows per role.
    [[nodiscard]] LoginTarget login_target(const storage::BackendTarget& backend, UpstreamFlavor flavor,
                                           bool custom_auth_dll) const;

private:
    Result<IdentitySnapshot> rename(std::string name);
    void adopt(const storage::AccountsDocument& document);

    storage::DocumentStore<storage::AccountsDocument>& accounts_;
    storage::DocumentStore<BackendLoginsDocument>& logins_;
    IRandom& random_;
    EventBus& events_;
    IdentitySnapshot current_;
};

}  // namespace rb::identity
