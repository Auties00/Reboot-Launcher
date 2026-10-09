#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"

namespace rb::os_macos::ipc {

// One blocking call on a thread of its own, for an API with no cancellable form; callers wait
// for it as long as they choose and may come back for the same result.
class PendingCall {
public:
    PendingCall() = default;
    // Joins the thread, however long the call still runs.
    ~PendingCall();
    PendingCall(const PendingCall&) = delete;
    PendingCall& operator=(const PendingCall&) = delete;

    // Runs `call` once; anything it throws becomes internal.bug. Fails when no thread can be
    // created, or while an earlier call's result is still uncollected.
    [[nodiscard]] Result<void> start(UniqueFunction<Result<void>()> call);
    // The call's result once it finished within `wait`, nullopt while it still runs. The result is
    // handed out once; the thread is joined before it is returned.
    [[nodiscard]] std::optional<Result<void>> wait_for(std::chrono::milliseconds wait);

private:
    std::mutex mutex_;
    std::condition_variable finished_;
    std::optional<Result<void>> result_;
    std::thread thread_;
};

}  // namespace rb::os_macos::ipc
