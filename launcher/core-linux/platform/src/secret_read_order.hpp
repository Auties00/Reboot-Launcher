#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/secret.hpp"

namespace rb::os_linux::platform {

// LibsecretSecretStore::get over its two backends. A fallback file is always newer than a keyring
// copy, since a successful keyring put removes the file and only a put that cannot reach the
// keyring writes one; so the file is read first, whatever the mode. In Os mode a file value moves
// into the keyring; the file value is returned even when that move fails. A file hit never
// probes, so File mode reads stay off the bus. A file miss falls through to the keyring, unless
// the keyring has gone, which reads as a miss.
// `Backends` has lookup_file, clear_file, lookup_os, store_os, os_active (no probe), os_reachable
// (probes in File mode) and fell_back.
template <class Backends>
[[nodiscard]] Result<std::optional<SecretBytes>> read_newest(Backends& backends, const std::string& key) {
    Result<std::optional<SecretBytes>> in_file = backends.lookup_file(key);
    if (!in_file) return in_file;
    if (*in_file) {
        if (backends.os_active() && backends.store_os(key, (*in_file)->reveal())) (void)backends.clear_file(key);
        return in_file;
    }
    if (!backends.os_reachable()) return std::nullopt;
    Result<std::optional<SecretBytes>> in_os = backends.lookup_os(key);
    if (!in_os && backends.fell_back(in_os.error())) return std::nullopt;
    return in_os;
}

}  // namespace rb::os_linux::platform
