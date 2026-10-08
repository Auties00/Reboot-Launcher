#pragma once

#include <mutex>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/testing/fault_plan.hpp"

namespace reboot::testing {

enum class DiskOperation : u8 { Volumes, VolumeOf };

// Covers no capability ids (decision testing-strategy).
// IDiskInfo over volumes the test adds. volume_of picks the longest mount that contains the path
// and fails with testing.no_volume (NotFound) when none does.
class FakeDiskInfo final : public ports::IDiskInfo {
public:
    Result<std::vector<ports::VolumeInfo>> volumes() override;
    Result<ports::VolumeInfo> volume_of(const NativePath& path) override;

    void add_volume(ports::VolumeInfo volume);
    // E.g. to fill a disk while an install is staging.
    void set_free_bytes(const NativePath& mount, u64 free_bytes);
    void clear();

    [[nodiscard]] FaultPlan<DiskOperation>& faults() noexcept { return faults_; }

private:
    mutable std::mutex mutex_;
    std::vector<ports::VolumeInfo> volumes_;
    FaultPlan<DiskOperation> faults_;
};

}  // namespace reboot::testing
