#pragma once

#include <optional>
#include <span>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::os_linux::platform {

// LibsecretSecretStore's put, get and erase over its two backends and the tombstones beside the
// fallback files. Precedence is fallback file, then tombstone, then keyring: a file is always
// newer than a keyring copy, since a keyring write removes it and only a write that cannot reach
// the keyring makes one; a tombstone marks an erase that could not reach the keyring, so a keyring
// copy under it is stale. Each step clears the lower backends only after the higher one holds the
// result, so a failure part way never brings an older value back.
// `Backends` has lookup_file, store_file, clear_file, tombstoned, write_tombstone, clear_tombstone,
// lookup_os, store_os, clear_os, os_active (no probe), os_reachable (probes in File mode) and
// fell_back.

// A keyring write clears the file and tombstone; a write that cannot reach the keyring goes to
// the file, which outranks any tombstone.
template <class Backends>
[[nodiscard]] Result<void> write_newest(Backends& backends, const std::string& key, std::span<const u8> value) {
    if (backends.os_reachable()) {
        Result<void> stored = backends.store_os(key, value);
        if (stored) {
            if (Result<void> untombed = backends.clear_tombstone(key); !untombed) return untombed;
            return backends.clear_file(key);
        }
        if (!backends.fell_back(stored.error())) return stored;
    }
    if (Result<void> written = backends.store_file(key, value); !written) return written;
    return backends.clear_tombstone(key);
}

// The file first in either mode; in Os mode a file value moves into the keyring, and the file
// value is returned even when that move fails. A file hit never probes, so File mode reads stay
// off the bus. A tombstone reads as a miss and, once the keyring answers, clears the keyring copy
// and itself. Otherwise the keyring, unless it has gone, which reads as a miss.
template <class Backends>
[[nodiscard]] Result<std::optional<SecretBytes>> read_newest(Backends& backends, const std::string& key) {
    Result<std::optional<SecretBytes>> in_file = backends.lookup_file(key);
    if (!in_file) return in_file;
    if (*in_file) {
        if (backends.os_active() && backends.store_os(key, (*in_file)->reveal()) && backends.clear_tombstone(key))
            (void)backends.clear_file(key);
        return in_file;
    }
    Result<bool> erased = backends.tombstoned(key);
    if (!erased) return std::unexpected(std::move(erased.error()));
    if (*erased) {
        if (backends.os_reachable()) {
            if (Result<void> cleared = backends.clear_os(key); cleared)
                (void)backends.clear_tombstone(key);
            else
                (void)backends.fell_back(cleared.error());
        }
        return std::nullopt;
    }
    if (!backends.os_reachable()) return std::nullopt;
    Result<std::optional<SecretBytes>> in_os = backends.lookup_os(key);
    if (!in_os && backends.fell_back(in_os.error())) return std::nullopt;
    return in_os;
}

// Clears the keyring while it answers; otherwise leaves a tombstone, written before the file
// goes, so the erase still wins over a keyring copy once the keyring is back. A missing key is
// success.
template <class Backends>
[[nodiscard]] Result<void> erase_everywhere(Backends& backends, const std::string& key) {
    if (backends.os_reachable()) {
        Result<void> cleared = backends.clear_os(key);
        if (cleared) {
            if (Result<void> removed = backends.clear_file(key); !removed) return removed;
            return backends.clear_tombstone(key);
        }
        if (!backends.fell_back(cleared.error())) return cleared;
    }
    if (Result<void> marked = backends.write_tombstone(key); !marked) return marked;
    return backends.clear_file(key);
}

}  // namespace rb::os_linux::platform
