#pragma once

#include <memory>
#include <string>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot {
class Executor;
class Redactor;
class WorkerPool;
}  // namespace reboot

namespace reboot::logging {

struct LogExportRequest {
    // An existing file is replaced only once the archive is complete.
    NativePath destination;
    // Written as runtime-summary.txt, through the Redactor like every log line.
    std::string runtime_summary;
};

struct LogExportResult {
    NativePath archive;
    u32 files = 0;
    // Files pruned or removed between listing and reading.
    u32 skipped = 0;
    u64 bytes = 0;
};

// Capabilities: logging-diagnostics.log-and-errors.
// Strand-only. Zips the logs on the WorkerPool, every line through the Redactor; a partial archive is removed.
// Proton writes outside the Logger, so for its logs that pass is the only redaction.
class LogExporter {
public:
    LogExporter(NativePath logs_dir, const Redactor& redactor, OpRegistry& ops, WorkerPool& workers, Executor& strand);
    ~LogExporter();
    LogExporter(const LogExporter&) = delete;
    LogExporter& operator=(const LogExporter&) = delete;

    // An OpKind::LogExport op completing with LogExportResult; `destination` must be an absolute .zip outside logs.
    [[nodiscard]] Result<OpHandle> start_export(LogExportRequest request, DisconnectPolicy policy);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::logging
