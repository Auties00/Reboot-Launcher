#pragma once

#include <map>
#include <mutex>
#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/testing/fault_plan.hpp"

namespace reboot::testing {

enum class RegistrarOperation : u8 { Status, Apply, Remove };

// Covers no capability ids (decision testing-strategy).
// IIntegrationRegistrar over a state per kind. apply makes a kind Ours for that exe, remove makes
// it Absent; set_state plants a Foreign or Stale entry, as another launcher or a moved install would.
class FakeRegistrar final : public ports::IIntegrationRegistrar {
public:
    Result<ports::IntegrationStatus> status(ports::IntegrationKind kind) override;
    Result<void> apply(ports::IntegrationKind kind, const NativePath& exe) override;
    Result<void> remove(ports::IntegrationKind kind) override;

    void set_state(ports::IntegrationKind kind, ports::IntegrationState state);
    // The exe the kind was last applied for.
    [[nodiscard]] std::optional<NativePath> applied_exe(ports::IntegrationKind kind) const;
    [[nodiscard]] FaultPlan<RegistrarOperation>& faults() noexcept { return faults_; }

private:
    struct Entry {
        ports::IntegrationState state = ports::IntegrationState::Absent;
        std::optional<NativePath> exe;
    };

    mutable std::mutex mutex_;
    std::map<ports::IntegrationKind, Entry> entries_;
    FaultPlan<RegistrarOperation> faults_;
};

}  // namespace reboot::testing
