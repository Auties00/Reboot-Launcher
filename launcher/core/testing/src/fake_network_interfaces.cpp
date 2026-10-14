#include "reboot/testing/fake_network_interfaces.hpp"

#include <mutex>
#include <utility>

namespace rb::testing {

Result<std::vector<ports::LocalAddress>> FakeNetworkInterfaces::local_addresses() {
    const std::scoped_lock lock(mutex_);
    return answer_;
}

void FakeNetworkInterfaces::set(Result<std::vector<ports::LocalAddress>> answer) {
    const std::scoped_lock lock(mutex_);
    answer_ = std::move(answer);
}

}  // namespace rb::testing
