#include "reboot/testing/fake_system_info.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace reboot::testing {

ports::OsInfo FakeSystemInfo::os() const {
    const std::scoped_lock lock(mutex_);
    return facts_.os;
}

bool FakeSystemInfo::elevated() const {
    const std::scoped_lock lock(mutex_);
    return facts_.elevated;
}

std::string FakeSystemInfo::os_session() const {
    const std::scoped_lock lock(mutex_);
    return facts_.os_session;
}

bool FakeSystemInfo::under_steam_reaper() const {
    const std::scoped_lock lock(mutex_);
    return facts_.under_steam_reaper;
}

std::optional<NativePath> FakeSystemInfo::ca_bundle() const {
    const std::scoped_lock lock(mutex_);
    return facts_.ca_bundle;
}

void FakeSystemInfo::set(FakeSystemFacts facts) {
    const std::scoped_lock lock(mutex_);
    facts_ = std::move(facts);
}

}  // namespace reboot::testing
