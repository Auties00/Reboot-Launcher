#include "pending_call.hpp"

#include <system_error>
#include <utility>

#include "reboot/posix/posix_error.hpp"

namespace reboot::os_macos::ipc {

PendingCall::~PendingCall() {
    if (thread_.joinable()) thread_.join();
}

Result<void> PendingCall::start(UniqueFunction<Result<void>()> call) {
    if (thread_.joinable()) return std::unexpected(internal_bug("PendingCall::start"));
    try {
        thread_ = std::thread([this, run = std::move(call)]() mutable {
            Result<void> result;
            try {
                result = run();
            } catch (...) {
                result = std::unexpected(internal_bug("PendingCall"));
            }
            {
                const std::lock_guard lock(mutex_);
                result_ = std::move(result);
            }
            finished_.notify_all();
        });
    } catch (const std::system_error& error) {
        return std::unexpected(posix::call_failed("pthread_create", error.code().value()));
    }
    return {};
}

std::optional<Result<void>> PendingCall::wait_for(std::chrono::milliseconds wait) {
    std::optional<Result<void>> result;
    {
        std::unique_lock lock(mutex_);
        if (!finished_.wait_for(lock, wait, [this] { return result_.has_value(); })) return std::nullopt;
        result = std::exchange(result_, std::nullopt);
    }
    if (thread_.joinable()) thread_.join();
    return result;
}

}  // namespace reboot::os_macos::ipc
