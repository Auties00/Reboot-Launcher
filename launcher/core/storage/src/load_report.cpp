#include "reboot/storage/load_report.hpp"

#include <algorithm>

namespace rb::storage {

StorageMode combined_mode(std::span<const LoadReport> reports) noexcept {
    // Enumerators run from least to most restricted.
    StorageMode mode = StorageMode::ReadWrite;
    for (const LoadReport& report : reports) mode = std::max(mode, report.mode);
    return mode;
}

}  // namespace rb::storage
