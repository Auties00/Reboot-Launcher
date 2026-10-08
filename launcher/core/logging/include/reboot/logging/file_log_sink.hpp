#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/logging/log_file_names.hpp"
#include "reboot/logging/retention_policy.hpp"

namespace reboot {
class IClock;
}  // namespace reboot

namespace reboot::ports {
class ILogFileSystem;
}  // namespace reboot::ports

namespace reboot::logging {

class LogFilesInUse;
class WineLogFiles;

inline constexpr u64 kSessionLogRollBytes = 8ull << 20;

struct FileLogOptions {
    NativePath dir;
    LogFileGroup group;
    // A constant such as "engine"; one is_valid_log_role rejects is internal.bug.
    std::string role;
    u64 roll_bytes = kSessionLogRollBytes;
    RetentionPolicy retention;
};

struct FileLogStatus {
    NativePath current_file;
    u32 part = 0;
    u64 failed_writes = 0;
    std::optional<Diagnostic> last_failure;
};

// Capabilities: logging-diagnostics.log-and-errors, logging-diagnostics.+41.
// Never truncates or renames a file; a roll creates the next part.
// A Wine-routed line goes to `wine` when given and it takes the line, else into the part.
class FileLogSink final : public LogSink {
public:
    // Creates `dir` through the port, so no ACL is set. Prunes after opening and every roll, ignoring errors.
    [[nodiscard]] static Result<std::unique_ptr<FileLogSink>> open(FileLogOptions options, const IClock& clock,
                                                                   ports::ILogFileSystem& files,
                                                                   LogFilesInUse& in_use,
                                                                   std::unique_ptr<WineLogFiles> wine);

    ~FileLogSink() override;
    FileLogSink(const FileLogSink&) = delete;
    FileLogSink& operator=(const FileLogSink&) = delete;

    void write(std::span<const LogRecord> records) override;
    void flush() override;

    // Thread-safe.
    [[nodiscard]] FileLogStatus status() const;
    // Null when opened without one (Windows).
    [[nodiscard]] WineLogFiles* wine_logs() noexcept;

    // Writer thread, no lock held: a part's or Wine log's first failed write after it opened or last succeeded.
    // Thread-safe; once it returns, the previous callback is no longer running.
    void set_on_failure(UniqueFunction<void(const Diagnostic&)> on_failure);

private:
    struct Impl;
    explicit FileLogSink(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::logging
