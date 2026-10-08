#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace reboot::catalog {

// What the catalog generator probed; the extractor still picks the format from magic bytes.
// Unrecognized is a value this build cannot read, and such an entry is never installable.
enum class ArchiveFormat : u8 { Unrecognized, Zip, Rar, SevenZip };

// ZipStored: the payload is the single stored entry of a ZIP, read in place through an offset window.
enum class ArchiveContainer : u8 { Unrecognized, None, ZipStored };

// Unavailable: the mirror is down for now. Withdrawn: pulled for good. Neither is installable.
enum class Availability : u8 { Available, Unavailable, Withdrawn };

struct CatalogEntry {
    CatalogEntryId id;
    GameVersion version;
    std::optional<Changelist> changelist;
    // Cosmetic and never parsed, such as "Fortnite 12.41"; empty shows the id.
    std::string display_name;
    // Extra names users know the build by, such as "cert" or "3.50.1".
    std::vector<std::string> aliases;
    std::string url;
    ArchiveFormat format = ArchiveFormat::Unrecognized;
    ArchiveContainer container = ArchiveContainer::Unrecognized;
    u64 archive_size = 0;
    std::optional<u64> installed_size;
    // Optional: the archive CRCs already verify content during extraction.
    std::optional<std::array<u8, 32>> sha256;
    Availability availability = Availability::Unavailable;

    // Available and of a known format and container. The version cap is not applied here: the
    // generator lists only supported builds, and builds applies support::above_version_cap.
    [[nodiscard]] bool installable() const noexcept {
        return availability == Availability::Available && format != ArchiveFormat::Unrecognized &&
               container != ArchiveContainer::Unrecognized;
    }

    bool operator==(const CatalogEntry&) const = default;
};

}  // namespace reboot::catalog
