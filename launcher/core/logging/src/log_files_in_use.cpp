#include "reboot/logging/log_files_in_use.hpp"

#include <algorithm>

namespace reboot::logging {

void LogFilesInUse::add(NativePath file) {
    const std::lock_guard lock(mutex_);
    files_.push_back(std::move(file));
}

void LogFilesInUse::remove(const NativePath& file) {
    const std::lock_guard lock(mutex_);
    if (const auto it = std::ranges::find(files_, file); it != files_.end()) files_.erase(it);
}

std::vector<NativePath> LogFilesInUse::snapshot() const {
    const std::lock_guard lock(mutex_);
    return files_;
}

}  // namespace reboot::logging
