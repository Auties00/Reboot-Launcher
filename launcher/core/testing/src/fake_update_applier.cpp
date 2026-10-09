#include "reboot/testing/fake_update_applier.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace reboot::testing {

Result<void> FakeUpdateApplier::stage(const NativePath& package) {
    if (auto error = faults_.take(UpdateApplierOperation::Stage)) return std::unexpected(std::move(*error));
    if (fs_ != nullptr)
        if (auto found = fs_->revision(package); !found) return std::unexpected(std::move(found.error()));
    const std::scoped_lock lock(mutex_);
    staged_ = package;
    return {};
}

Result<void> FakeUpdateApplier::apply_and_restart(std::vector<std::string> args) {
    if (auto error = faults_.take(UpdateApplierOperation::ApplyAndRestart)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    restarted_with_ = std::move(args);
    return {};
}

bool FakeUpdateApplier::supports_in_place() const {
    const std::scoped_lock lock(mutex_);
    return in_place_;
}

std::optional<NativePath> FakeUpdateApplier::staged() const {
    const std::scoped_lock lock(mutex_);
    return staged_;
}

std::optional<std::vector<std::string>> FakeUpdateApplier::restarted_with() const {
    const std::scoped_lock lock(mutex_);
    return restarted_with_;
}

}  // namespace reboot::testing
