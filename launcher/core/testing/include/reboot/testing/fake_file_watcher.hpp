#pragma once

#include <cstddef>
#include <memory>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::testing {

class InMemoryFileSystem;

// Covers no capability ids (decision testing-strategy).
// IFileWatcher fed by the test or a followed InMemoryFileSystem, posting to `deliver_on`; a destroyed
// WatchHandle gets nothing more, even for changes already posted.
class FakeFileWatcher final : public ports::IFileWatcher {
public:
    explicit FakeFileWatcher(Executor& deliver_on);
    ~FakeFileWatcher() override;
    FakeFileWatcher(const FakeFileWatcher&) = delete;
    FakeFileWatcher& operator=(const FakeFileWatcher&) = delete;

    Result<ports::WatchHandle> watch(const NativePath& dir, UniqueFunction<void(ports::FileChange)> on_change) override;

    // Delivers to every live watch whose directory contains `change.path`.
    void emit(ports::FileChange change);
    // Forwards every mutation of `fs`; replaces its change listener.
    void follow(InMemoryFileSystem& fs);
    // Drops changes instead of delivering them, as an overflowed OS queue does.
    void set_overflowing(bool overflowing);
    void fail_next_watch(Diagnostic error);

    [[nodiscard]] std::size_t live_watches() const;

private:
    struct State;
    // Shared with the handles and with posted deliveries, which may outlive the watcher.
    std::shared_ptr<State> state_;
};

}  // namespace rb::testing
