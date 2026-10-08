#pragma once

#include "reboot/foundation/types.hpp"
#include "reboot/publish/host_identity.hpp"

namespace reboot::publish {

class HostIdentityStore;

// Marks an identity as used by one live publication; released on destruction. Only one hold per
// profile exists, which is what keeps a second session from superseding the first with CONFLICT.
// The store must outlive every hold.
class IdentityHold {
public:
    IdentityHold() = default;
    IdentityHold(IdentityHold&& other) noexcept;
    IdentityHold& operator=(IdentityHold&& other) noexcept;
    IdentityHold(const IdentityHold&) = delete;
    IdentityHold& operator=(const IdentityHold&) = delete;
    ~IdentityHold();

    [[nodiscard]] bool held() const noexcept { return store_ != nullptr; }
    [[nodiscard]] const HostProfileId& profile() const noexcept { return profile_; }
    // Valid while held and until the next HostIdentityStore call: save_token() and rotate() change
    // the identity, so read it again after either.
    [[nodiscard]] const HostIdentity& identity() const;

private:
    friend class HostIdentityStore;
    IdentityHold(HostIdentityStore& store, HostProfileId profile) : store_(&store), profile_(profile) {}

    HostIdentityStore* store_ = nullptr;
    HostProfileId profile_;
};

}  // namespace reboot::publish
