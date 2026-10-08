#include "reboot/testing/fake_caller_context.hpp"

#include <mutex>
#include <utility>
#include <vector>

namespace reboot::testing {

ports::CallerContext FakeCallerContext::capture() const {
    const std::scoped_lock lock(mutex_);
    return context_;
}

void FakeCallerContext::allow_foreground(u32 pid) {
    const std::scoped_lock lock(mutex_);
    foreground_allowed_.push_back(pid);
}

void FakeCallerContext::set(ports::CallerContext context) {
    const std::scoped_lock lock(mutex_);
    context_ = std::move(context);
}

std::vector<u32> FakeCallerContext::foreground_allowed() const {
    const std::scoped_lock lock(mutex_);
    return foreground_allowed_;
}

}  // namespace reboot::testing
