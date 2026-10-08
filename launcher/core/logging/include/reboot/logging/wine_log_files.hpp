#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/logging/log_file_names.hpp"

namespace reboot::ports {
class ILogFileSystem;
}  // namespace reboot::ports

namespace reboot::logging {

class LogFilesInUse;

inline constexpr u64 kWineLogCapBytes = 64ull << 20;

// Category Wine with a session: LogRing skips it so it cannot crowd out launcher lines.
[[nodiscard]] bool routes_to_wine_log(const LogRecord& record) noexcept;

struct WineLogOptions {
    NativePath dir;
    // The engine run's group, shared with its FileLogSink.
    LogFileGroup group;
    u64 cap_bytes = kWineLogCapBytes;
};

struct WineLogStatus {
    u64 dropped_lines = 0;
    u64 failed_writes = 0;
    std::optional<Diagnostic> last_failure;
};

// Capabilities: logging-diagnostics.log-and-errors.
// macOS and Linux; owned by FileLogSink and called on the writer thread unless noted.
// A session's file opens on its first line and stays in `in_use` while open.
class WineLogFiles {
public:
    WineLogFiles(WineLogOptions options, ports::ILogFileSystem& files, LogFilesInUse& in_use);
    ~WineLogFiles();
    WineLogFiles(const WineLogFiles&) = delete;
    WineLogFiles& operator=(const WineLogFiles&) = delete;

    // On error the caller keeps `line`; a failed session is not retried until close_session.
    // Past the cap: one truncation line, then lines are counted as dropped.
    [[nodiscard]] Result<void> append(const SessionId& session, std::string_view line);
    void flush();

    // Thread-safe. A later line for the session reopens its file for append.
    void close_session(const SessionId& session);
    // Thread-safe.
    [[nodiscard]] WineLogStatus status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::logging
