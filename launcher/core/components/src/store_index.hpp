#pragma once

#include <span>
#include <vector>

#include "reboot/components/component_ref.hpp"
#include "reboot/components/payload_entry.hpp"
#include "reboot/components/remote_file.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::components {

// One stored version. It keeps its artifacts' urls, so a stored version can be re-fetched after the
// manifest stopped naming it.
struct StoreEntry {
    ComponentRef ref;
    // Payload: the files of the roles this platform needs.
    u16 payload_abi = 0;
    std::vector<PayloadFile> files;
    // Runtime.
    RuntimeKind runtime_kind{};
    RemoteFile archive;
    // Payload: the file sizes; runtime: the unpacked bytes.
    u64 size_bytes = 0;
    bool last_good = false;
    // Not persisted: a file failed its last check.
    bool broken = false;
};

// data/components/index.json: {"schema": 1, "payloads": [...], "runtimes": [...]}. A newer schema
// fails with ErrorKind::Unsupported.
[[nodiscard]] Result<std::vector<StoreEntry>> parse_store_index(std::span<const u8> bytes);
[[nodiscard]] std::vector<u8> serialize_store_index(const std::vector<StoreEntry>& entries);

}  // namespace rb::components
