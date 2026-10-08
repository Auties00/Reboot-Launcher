#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/testing/fault_plan.hpp"

namespace reboot::testing {

enum class UpdateApplierOperation : u8 { Stage, ApplyAndRestart };

// Covers no capability ids (decision testing-strategy).
// IUpdateApplier that records. apply_and_restart returns success instead of exiting, so a test
// asserts on the restart arguments and on what the updater wrote first.
class FakeUpdateApplier final : public ports::IUpdateApplier {
public:
    explicit FakeUpdateApplier(bool in_place = true) : in_place_(in_place) {}

    Result<void> stage(const NativePath& package) override;
    Result<void> apply_and_restart(std::vector<std::string> args) override;
    [[nodiscard]] bool supports_in_place() const override;

    [[nodiscard]] std::optional<NativePath> staged() const;
    [[nodiscard]] std::optional<std::vector<std::string>> restarted_with() const;
    [[nodiscard]] FaultPlan<UpdateApplierOperation>& faults() noexcept { return faults_; }

private:
    mutable std::mutex mutex_;
    bool in_place_;
    std::optional<NativePath> staged_;
    std::optional<std::vector<std::string>> restarted_with_;
    FaultPlan<UpdateApplierOperation> faults_;
};

}  // namespace reboot::testing
