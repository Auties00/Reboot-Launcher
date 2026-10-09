#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::testing {

// Covers no capability ids (decisions testing-strategy, async-event-model).
// IEngineStarter answering from a script (the last answer repeats; Started when empty), so
// autostart, AwaitingUser, CannotDetach and the refusals can each be driven.
class FakeEngineStarter final : public ports::IEngineStarter {
public:
    Result<ports::StartResult> ensure_started(const NativePath& engine_exe, const DataRoot& root) override;

    void script(std::vector<Result<ports::StartResult>> answers);
    // Runs for each Started, e.g. to bring an in-process engine up on InMemoryIpc.
    void on_started(UniqueFunction<void(const NativePath& engine_exe, const DataRoot& root)> hook);

    [[nodiscard]] std::size_t calls() const;

private:
    mutable std::mutex mutex_;
    std::vector<Result<ports::StartResult>> answers_;
    std::size_t calls_ = 0;
    UniqueFunction<void(const NativePath&, const DataRoot&)> on_started_;
};

}  // namespace rb::testing
