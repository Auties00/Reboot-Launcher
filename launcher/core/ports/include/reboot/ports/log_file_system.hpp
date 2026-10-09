#pragma once

#include <chrono>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::ports {

// An open log file that is only ever appended to; closes on destruction.
class LogFile {
public:
    class Handle {
    public:
        virtual ~Handle() = default;
        // Writes all of `bytes` at the end of the file, retrying short writes.
        virtual Result<void> append(std::span<const u8> bytes) = 0;
        // Pushes what was written to the device.
        virtual Result<void> flush() = 0;
    };

    LogFile() = default;
    LogFile(std::unique_ptr<Handle> handle, u64 size) : handle_(std::move(handle)), size_(size) {}

    [[nodiscard]] bool is_open() const noexcept { return handle_ != nullptr; }
    // The size when opened plus every append that succeeded.
    [[nodiscard]] u64 size() const noexcept { return size_; }

    Result<void> append(std::span<const u8> bytes) {
        Result<void> appended = handle_->append(bytes);
        if (appended) size_ += bytes.size();
        return appended;
    }
    Result<void> flush() { return handle_->flush(); }
    void close() noexcept { handle_.reset(); }

private:
    std::unique_ptr<Handle> handle_;
    u64 size_ = 0;
};

struct LogDirEntry {
    NativePath path;
    u64 size = 0;
    std::chrono::system_clock::time_point modified;
};

// Blocking; called from the logger's writer thread. POSIX creates files 0600. On Windows an open
// file stays readable, writable and deletable through other handles, so export and pruning work.
class ILogFileSystem {
public:
    virtual ~ILogFileSystem() = default;

    // Missing parents too, with default permissions.
    virtual Result<void> create_directories(const NativePath& dir) = 0;
    // Creates a missing file; never truncates or follows a final link.
    virtual Result<LogFile> open_append(const NativePath& path) = 0;
    // Regular files directly in `dir`; links are skipped.
    virtual Result<std::vector<LogDirEntry>> list(const NativePath& dir) = 0;
    virtual Result<void> remove(const NativePath& path) = 0;
};

}  // namespace reboot::ports
