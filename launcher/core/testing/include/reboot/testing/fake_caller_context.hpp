#pragma once

#include <mutex>
#include <utility>
#include <vector>

#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::testing {

// Covers no capability ids (decision testing-strategy).
// ICallerContextProbe with a context the test sets, e.g. another OS session or an elevated caller,
// recording every pid allowed to take the foreground.
class FakeCallerContext final : public ports::ICallerContextProbe {
public:
    explicit FakeCallerContext(ports::CallerContext context = {"1", false, true, {}}) : context_(std::move(context)) {}

    [[nodiscard]] ports::CallerContext capture() const override;
    void allow_foreground(u32 pid) override;

    void set(ports::CallerContext context);
    [[nodiscard]] std::vector<u32> foreground_allowed() const;

private:
    mutable std::mutex mutex_;
    ports::CallerContext context_;
    std::vector<u32> foreground_allowed_;
};

}  // namespace rb::testing
