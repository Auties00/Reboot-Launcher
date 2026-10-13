#pragma once

#include <cstddef>
#include <expected>
#include <mutex>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::testing {

// One scripted answer. A plain struct, since libc++ cannot hold std::expected in a vector its
// callers build (the operator== constraint recurses).
struct StartAnswer {
    StartAnswer(ports::StartResult started) : result(started) {}
    StartAnswer(std::unexpected<Diagnostic> refused) : result(std::move(refused)) {}

    Result<ports::StartResult> result;
};

// Covers no capability ids (decisions testing-strategy, async-event-model).
// IEngineStarter answering from a script (the last answer repeats; Started when empty), so
// autostart, AwaitingUser, CannotDetach and the refusals can each be driven.
class FakeEngineStarter final : public ports::IEngineStarter {
public:
    Result<ports::StartResult> ensure_started(const NativePath& engine_exe, const DataRoot& root) override;

    void script(std::vector<StartAnswer> answers);
    // Runs for each Started, e.g. to bring an in-process engine up on InMemoryIpc.
    void on_started(UniqueFunction<void(const NativePath& engine_exe, const DataRoot& root)> hook);

    [[nodiscard]] std::size_t calls() const;

private:
    mutable std::mutex mutex_;
    std::vector<StartAnswer> answers_;
    std::size_t calls_ = 0;
    UniqueFunction<void(const NativePath&, const DataRoot&)> on_started_;
};

}  // namespace rb::testing
