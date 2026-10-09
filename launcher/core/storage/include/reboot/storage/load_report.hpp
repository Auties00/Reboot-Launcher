#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::storage {

// HelloAck.storage_mode. ReadOnly: written at a newer schema. InMemory: nothing reaches disk.
using StorageMode = contracts::ipc::StorageMode;

// Backup: the primary was missing or did not parse but <file>.bak did. Defaults: neither parsed.
enum class LoadSource : u8 { Fresh, Primary, Backup, Defaults };

// A stored value replaced by its default. `path` is a key id or a member path like "builds[3].root".
struct ValueIssue {
    std::string path;
    Diagnostic reason;
};

// A load never fails: every problem ends up here and in the mode.
struct LoadReport {
    std::string document;
    StorageMode mode = StorageMode::ReadWrite;
    LoadSource source = LoadSource::Fresh;
    u32 schema_on_disk = 0;
    // <file>.corrupt-<UTC timestamp>, a copy of the unreadable primary.
    std::optional<NativePath> quarantined_to;
    // <name>.v<N>.json, saved before a document at an older schema was upgraded.
    std::optional<NativePath> schema_backup;
    // Why the mode is not ReadWrite, or why the primary was not used.
    std::optional<Diagnostic> reason;
    std::vector<ValueIssue> issues;
};

// The most restricted mode of all stores, which HelloAck reports.
[[nodiscard]] StorageMode combined_mode(std::span<const LoadReport> reports) noexcept;

}  // namespace rb::storage
