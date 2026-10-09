#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/testing/fault_plan.hpp"

namespace rb::testing {

enum class FsOperation : u8 {
    AtomicReplace,
    ReadAll,
    LockExclusive,
    RestrictToOwner,
    OpenDenyWrite,
    Revision,
    CreateDirsOwnerOnly,
    RemoveTree,
    ReadShared,
};

// Covers no capability ids (decision testing-strategy).
// IFileSystem over a tree in memory, behaving as run_file_system_conformance expects of the real
// adapters. Thread-safe, since the port is called from workers; paths compare lexically_normal().
class InMemoryFileSystem final : public ports::IFileSystem {
public:
    // mtimes from a counter that moves one second per mutation.
    InMemoryFileSystem();
    // mtimes from `clock`.
    explicit InMemoryFileSystem(const IClock& clock);
    ~InMemoryFileSystem() override;
    InMemoryFileSystem(const InMemoryFileSystem&) = delete;
    InMemoryFileSystem& operator=(const InMemoryFileSystem&) = delete;

    Result<void> atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) override;
    Result<std::vector<u8>> read_all(const NativePath& path) override;
    // A held lock fails a non-waiting call with Conflict. A waiting call blocks up to the lock wait
    // limit, then fails with testing.lock_wait_exceeded, so a test never hangs on its own lock.
    Result<ports::FileLock> lock_exclusive(const NativePath& path, bool wait) override;
    Result<void> restrict_to_owner(const NativePath& path) override;
    Result<ports::HeldFile> open_deny_write(const NativePath& path) override;
    Result<ports::FileRevision> revision(const NativePath& path) override;
    Result<ports::SharedRead> read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) override;
    Result<void> create_dirs_owner_only(const NativePath& path) override;
    Result<void> remove_tree(const NativePath& path) override;

    // Setup and inspection. They create missing parents and ignore faults and locks.
    void write(const NativePath& path, std::span<const u8> bytes);
    void write_text(const NativePath& path, std::string_view text);
    // Grows a file in place, keeping its id, as a writer holding it open does; creates it when missing.
    void append(const NativePath& path, std::span<const u8> bytes);
    void make_dir(const NativePath& path);
    void make_symlink(const NativePath& link, const NativePath& target);
    [[nodiscard]] std::optional<std::vector<u8>> contents(const NativePath& path) const;
    [[nodiscard]] std::optional<std::string> text(const NativePath& path) const;
    [[nodiscard]] bool exists(const NativePath& path) const;
    [[nodiscard]] bool is_dir(const NativePath& path) const;
    [[nodiscard]] bool owner_only(const NativePath& path) const;
    [[nodiscard]] bool locked(const NativePath& path) const;
    // Direct children, sorted.
    [[nodiscard]] std::vector<NativePath> list(const NativePath& dir) const;

    // Real time a waiting lock_exclusive blocks; 0 (the default) fails at once, as on a
    // single-threaded DeterministicRuntime nothing else could release the lock.
    void set_lock_wait_limit(std::chrono::milliseconds limit);

    // The next atomic_replace of `target` leaves only its temporary file behind and fails, as a
    // crash between the write and the rename would.
    void crash_during_next_replace(const NativePath& target);

    [[nodiscard]] FaultPlan<FsOperation>& faults() noexcept { return faults_; }

    // Every successful mutation, as a watcher of the parent directory would see it. Runs on the
    // mutating thread, outside the internal lock.
    void set_change_listener(UniqueFunction<void(const ports::FileChange&)> listener);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    FaultPlan<FsOperation> faults_;
};

}  // namespace rb::testing
