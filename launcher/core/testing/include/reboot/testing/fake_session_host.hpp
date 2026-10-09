#pragma once

#include <memory>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/session_host.hpp"
#include "reboot/testing/fake_session_control.hpp"
#include "reboot/testing/fault_plan.hpp"

namespace rb::testing {

enum class SessionHostOperation : u8 { Launch, Inject, Resume };

// Covers no capability ids (decision testing-strategy).
// ISessionHost for play tests, standing in for Win32SessionHost and compat::WineSessionHost. A launch
// only records the request; the test acts through its FakeSessionControl.
class FakeSessionHost final : public ports::ISessionHost {
public:
    explicit FakeSessionHost(Executor& io) : io_(io) {}

    Result<std::unique_ptr<ports::IGameSession>> launch(const ports::SessionLaunch& launch,
                                                        UniqueFunction<void(ports::SessionHostEvent)> on_event) override;

    // Let a test spawn and connect a FakeClientDll the way a real game with our DLL would.
    void on_launch(UniqueFunction<void(FakeSessionControl&)> hook);
    void on_resume(UniqueFunction<void(FakeSessionControl&)> hook);

    [[nodiscard]] std::vector<FakeSessionControl*> sessions() const;
    [[nodiscard]] FakeSessionControl* last() const;
    [[nodiscard]] FaultPlan<SessionHostOperation>& faults() noexcept { return faults_; }

private:
    friend class FakeSessionControl;

    Executor& io_;
    std::vector<std::unique_ptr<FakeSessionControl>> sessions_;
    UniqueFunction<void(FakeSessionControl&)> on_launch_;
    UniqueFunction<void(FakeSessionControl&)> on_resume_;
    FaultPlan<SessionHostOperation> faults_;
};

}  // namespace rb::testing
