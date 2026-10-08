#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/testing/fault_plan.hpp"

namespace reboot::testing {

enum class PrereqOperation : u8 { Remediate };

// Covers no capability ids (decision testing-strategy).
// IPrerequisiteProbe over statuses the test sets. A successful remediate marks that id met, so a
// remediate-then-recheck flow can be followed; an unknown id fails with testing.not_found.
class FakePrereqs final : public ports::IPrerequisiteProbe {
public:
    std::vector<ports::PrerequisiteStatus> check() override;
    Result<void> remediate(std::string_view id) override;

    void set(std::vector<ports::PrerequisiteStatus> statuses);
    [[nodiscard]] std::vector<std::string> remediated() const;
    [[nodiscard]] FaultPlan<PrereqOperation>& faults() noexcept { return faults_; }

private:
    mutable std::mutex mutex_;
    std::vector<ports::PrerequisiteStatus> statuses_;
    std::vector<std::string> remediated_;
    FaultPlan<PrereqOperation> faults_;
};

}  // namespace reboot::testing
