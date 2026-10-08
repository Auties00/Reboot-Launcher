#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/json/object.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/storage/load_report.hpp"

namespace reboot::storage {

struct LibraryEntry {
    BuildId id;
    std::string name;
    // Always the host-native build root; the engine maps it into Wine at launch.
    NativePath root;
    // Unset until detected, or when detection failed.
    std::optional<GameVersion> version;
    std::optional<Changelist> changelist;
    std::optional<CatalogEntryId> catalog_entry;
    std::chrono::system_clock::time_point added_at;
    // Imported from another OS or a missing drive; kept and shown, never dropped.
    bool needs_relocation = false;

    bool operator==(const LibraryEntry&) const = default;
};

// Capabilities: settings-storage.game-store.
// data/library.json, owned by builds. Selections are by id; one naming a missing entry reads as unset.
struct LibraryDocument {
    static constexpr std::string_view kName = "library";
    static constexpr u32 kSchema = 1;

    std::vector<LibraryEntry> builds;
    std::optional<BuildId> client_selection;
    std::optional<BuildId> host_selection;
    boost::json::object unknown;

    // An entry with a bad id, name or root is dropped with a ValueIssue, the rest are kept.
    [[nodiscard]] static LibraryDocument read(const boost::json::object& values, std::vector<ValueIssue>& issues);
    [[nodiscard]] boost::json::object write() const;
    [[nodiscard]] static Result<boost::json::object> upgrade(boost::json::object values, u32 from_schema);
};

}  // namespace reboot::storage
