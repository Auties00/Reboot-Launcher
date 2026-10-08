#pragma once

#include <mutex>
#include <vector>

#include "reboot/foundation/native_path.hpp"

namespace reboot::logging {

// Capabilities: logging-diagnostics.log-and-errors.
// Thread-safe. Pruning skips these, since on POSIX unlinking an open file loses its output.
// Holds the session parts, open Wine logs and a running Proton log under Verbose Wine logging.
class LogFilesInUse {
public:
    LogFilesInUse() = default;
    LogFilesInUse(const LogFilesInUse&) = delete;
    LogFilesInUse& operator=(const LogFilesInUse&) = delete;

    void add(NativePath file);
    // Undoes one add() of `file`.
    void remove(const NativePath& file);
    [[nodiscard]] std::vector<NativePath> snapshot() const;

private:
    mutable std::mutex mutex_;
    std::vector<NativePath> files_;
};

}  // namespace reboot::logging
