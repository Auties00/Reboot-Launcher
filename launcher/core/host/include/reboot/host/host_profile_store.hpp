#pragma once

#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/host/host_profile.hpp"
#include "reboot/host/host_profiles_document.hpp"
#include "reboot/storage/document_store.hpp"

namespace rb {
class IRandom;
}

namespace rb::host {

// Capabilities: settings-storage.hosting-store.
// Strand-only, over data/host-profiles.json; the engine is its only writer. Every write is one
// DocumentStore::update, so a profile is never half written. Liveness rules (ProfileBusy on
// delete, pushing operator changes) belong to HostService, which wraps these calls.
class HostProfileStore {
public:
    HostProfileStore(storage::DocumentStore<HostProfilesDocument>& store, IRandom& random);
    // Detaches from the store, which outlives this.
    ~HostProfileStore();
    HostProfileStore(const HostProfileStore&) = delete;
    HostProfileStore& operator=(const HostProfileStore&) = delete;

    // Engine startup, once the store is loaded: adds the auto profile and, when missing, a
    // default profile with Auto ports and `default_listing` (the host.listing setting). In
    // memory only if the store refuses.
    Result<void> ensure_builtin(HostListing default_listing);

    [[nodiscard]] std::vector<HostProfile> list() const;
    // host.profile_not_found.
    [[nodiscard]] Result<HostProfile> get(HostProfileId id) const;

    // The id and revision are assigned here; host.profile_name_taken for a duplicate name.
    Result<HostProfile> create(HostProfile draft);
    // host.profile_stale when `profile.revision` is not the stored one.
    Result<HostProfile> update(HostProfile profile);
    // host.builtin_profile for the default and auto profiles.
    Result<void> remove(HostProfileId id);
    // The Host reset group's records: reset_profile() on every profile, in one update.
    Result<void> reset();

    // Runs on the strand after a hand edit was reloaded.
    void set_on_reload(UniqueFunction<void()> on_reload);

private:
    // The document's profiles, plus any built-in it lacks: one the store refused or a hand edit dropped.
    [[nodiscard]] std::vector<HostProfile> merged() const;
    // Adds the missing built-ins in front of `profiles`; false when none was missing.
    bool add_builtins(std::vector<HostProfile>& profiles) const;
    Result<void> commit(std::vector<HostProfile> profiles);

    storage::DocumentStore<HostProfilesDocument>& store_;
    IRandom& random_;
    UniqueFunction<void()> on_reload_;
    // Set by ensure_builtin(); until then a missing built-in is not added.
    std::optional<HostListing> default_listing_;
};

}  // namespace rb::host
