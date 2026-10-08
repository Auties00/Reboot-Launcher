#include "reboot/testing/fake_prereqs.hpp"

#include <algorithm>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace reboot::testing {

std::vector<ports::PrerequisiteStatus> FakePrereqs::check() {
    const std::scoped_lock lock(mutex_);
    return statuses_;
}

Result<void> FakePrereqs::remediate(std::string_view id) {
    if (auto error = faults_.take(PrereqOperation::Remediate)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    const auto it = std::ranges::find(statuses_, id, &ports::PrerequisiteStatus::id);
    if (it == statuses_.end())
        return make_diag(kTestingDomain, msg::kNotFound).arg("path", id).kind(ErrorKind::NotFound).fail();
    it->met = true;
    remediated_.emplace_back(id);
    return {};
}

void FakePrereqs::set(std::vector<ports::PrerequisiteStatus> statuses) {
    const std::scoped_lock lock(mutex_);
    statuses_ = std::move(statuses);
}

std::vector<std::string> FakePrereqs::remediated() const {
    const std::scoped_lock lock(mutex_);
    return remediated_;
}

}  // namespace reboot::testing
