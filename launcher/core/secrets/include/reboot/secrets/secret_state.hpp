#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::secrets {

// FileStore is the owner-only file a store of kind File writes in place of the OS store.
enum class SecretLocation : u8 { Absent, Session, OsStore, FileStore };

struct SecretState {
    SecretLocation location = SecretLocation::Absent;
    // A Remember write is in flight; the location stays Session until it lands.
    bool write_pending = false;
    // Remember was asked for but the store is unavailable or refused the write, so the
    // secret is held for this engine run only.
    bool not_saved = false;

    [[nodiscard]] bool present() const noexcept { return location != SecretLocation::Absent; }

    bool operator==(const SecretState&) const = default;
};

}  // namespace rb::secrets
