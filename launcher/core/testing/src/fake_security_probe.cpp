#include "reboot/testing/fake_security_probe.hpp"

#include <mutex>
#include <optional>
#include <utility>

namespace reboot::testing {

Result<std::optional<ports::SecurityProducts>> FakeSecurityProbe::probe() {
    const std::scoped_lock lock(mutex_);
    return answer_;
}

void FakeSecurityProbe::set(Result<std::optional<ports::SecurityProducts>> answer) {
    const std::scoped_lock lock(mutex_);
    answer_ = std::move(answer);
}

}  // namespace reboot::testing
