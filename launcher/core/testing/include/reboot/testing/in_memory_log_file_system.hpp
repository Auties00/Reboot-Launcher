#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/log_file_system.hpp"
#include "reboot/testing/fault_plan.hpp"

namespace rb::testing {

enum class LogFsOperation : u8 { CreateDirectories, OpenAppend, Append, Flush, List, Remove };

// Covers no capability ids (decision testing-strategy).
// ILogFileSystem in memory. Thread-safe. A removed file's open handles write into a detached copy,
// as an unlinked file on POSIX does; paths compare lexically_normal().
class InMemoryLogFileSystem final : public ports::ILogFileSystem {
public:
    // Every mtime is `clock`'s system time at the change.
    explicit InMemoryLogFileSystem(const IClock& clock);
    ~InMemoryLogFileSystem() override;
    InMemoryLogFileSystem(const InMemoryLogFileSystem&) = delete;
    InMemoryLogFileSystem& operator=(const InMemoryLogFileSystem&) = delete;

    Result<void> create_directories(const NativePath& dir) override;
    Result<ports::LogFile> open_append(const NativePath& path) override;
    Result<std::vector<ports::LogDirEntry>> list(const NativePath& dir) override;
    Result<void> remove(const NativePath& path) override;

    // Setup and inspection; they create missing parents and ignore faults.
    void put(const NativePath& path, std::string_view text, std::chrono::system_clock::time_point modified);
    [[nodiscard]] std::optional<std::string> text(const NativePath& path) const;
    [[nodiscard]] bool exists(const NativePath& path) const;
    [[nodiscard]] bool is_dir(const NativePath& path) const;
    // Handles open on the file at `path`.
    [[nodiscard]] std::size_t open_handles(const NativePath& path) const;
    // File names directly in `dir`, sorted.
    [[nodiscard]] std::vector<std::string> file_names(const NativePath& dir) const;
    [[nodiscard]] u64 flushes() const;

    // Append and Flush faults reach handles that are already open.
    [[nodiscard]] FaultPlan<LogFsOperation>& faults() noexcept;

private:
    struct Impl;
    // Shared with open handles, which may outlive the file system.
    std::shared_ptr<Impl> impl_;
};

}  // namespace rb::testing
