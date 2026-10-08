#pragma once

#include <mutex>
#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::testing {

// Covers no capability ids (decision testing-strategy).
// ISecurityProductProbe answering what the test set: nullopt as on macOS and Linux, a product list
// with a Smart App Control state as on Windows, or an error as a WMI timeout would.
class FakeSecurityProbe final : public ports::ISecurityProductProbe {
public:
    Result<std::optional<ports::SecurityProducts>> probe() override;

    void set(Result<std::optional<ports::SecurityProducts>> answer);

private:
    mutable std::mutex mutex_;
    Result<std::optional<ports::SecurityProducts>> answer_{std::nullopt};
};

}  // namespace reboot::testing
