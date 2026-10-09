#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/testing/fault_plan.hpp"

namespace rb::testing {

enum class ShellOperation : u8 { OpenUrl, OpenPath, Reveal, Trash };

// Covers no capability ids (decision testing-strategy).
// IShellLauncher that records what it was asked. open_url refuses anything but https with
// testing.https_only (InvalidInput), as the real adapters refuse it; trash also removes the path
// through `fs` when one is given.
class FakeShell final : public ports::IShellLauncher {
public:
    explicit FakeShell(ports::IFileSystem* fs = nullptr) : fs_(fs) {}

    Result<void> open_url(std::string_view https_url) override;
    Result<void> open_path(const NativePath& path) override;
    Result<void> reveal(const NativePath& path) override;
    Result<void> trash(const NativePath& path) override;

    [[nodiscard]] std::vector<std::string> opened_urls() const;
    [[nodiscard]] std::vector<NativePath> opened_paths() const;
    [[nodiscard]] std::vector<NativePath> revealed() const;
    [[nodiscard]] std::vector<NativePath> trashed() const;
    [[nodiscard]] FaultPlan<ShellOperation>& faults() noexcept { return faults_; }

private:
    ports::IFileSystem* fs_;
    mutable std::mutex mutex_;
    std::vector<std::string> opened_urls_;
    std::vector<NativePath> opened_paths_;
    std::vector<NativePath> revealed_;
    std::vector<NativePath> trashed_;
    FaultPlan<ShellOperation> faults_;
};

}  // namespace rb::testing
