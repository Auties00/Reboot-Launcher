#include "runtime_recorder.hpp"

#include <algorithm>
#include <utility>

#include "reboot/engine/engine_info.hpp"
#include "reboot/foundation/log.hpp"

namespace reboot::engine {

namespace {

[[nodiscard]] bool same_process(const process::ChildRecord& a, const process::ChildRecord& b) noexcept {
    return a.pid == b.pid && a.created == b.created;
}

[[nodiscard]] Result<void> as_void(Result<u64> revision) {
    if (!revision) return std::unexpected(std::move(revision.error()));
    return {};
}

}  // namespace

std::vector<process::ChildRecord> RuntimeRecorder::recorded_children() const { return document_.get().children; }

Result<void> RuntimeRecorder::forget(const std::vector<process::ChildRecord>& reaped) {
    if (reaped.empty()) return {};
    return as_void(document_.update([&reaped](storage::RuntimeDocument& document) {
        std::erase_if(document.children, [&reaped](const process::ChildRecord& child) {
            return std::ranges::any_of(reaped, [&child](const process::ChildRecord& gone) { return same_process(gone, child); });
        });
    }));
}

Result<void> RuntimeRecorder::write_started(const EngineInfo& info) {
    return as_void(document_.update([&info](storage::RuntimeDocument& document) {
        document.engine_pid = info.self.pid;
        document.engine_created = info.self.created;
        document.engine_build = info.build;
        document.origin = info.origin;
        document.endpoint = NativePath(info.endpoint);
    }));
}

Result<void> RuntimeRecorder::set_ports(std::vector<storage::EnginePort> ports) {
    return as_void(document_.update(
        [ports = std::move(ports)](storage::RuntimeDocument& document) mutable { document.engine_ports = std::move(ports); }));
}

process::ChildRecordCallback RuntimeRecorder::recorder() {
    return [this](const process::ChildRecord& child, process::RecordChange change) { record(child, change); };
}

void RuntimeRecorder::record(const process::ChildRecord& child, process::RecordChange change) {
    Result<u64> written = document_.update([&child, change](storage::RuntimeDocument& document) {
        std::erase_if(document.children, [&child](const process::ChildRecord& recorded) { return same_process(recorded, child); });
        if (change == process::RecordChange::Spawned) document.children.push_back(child);
    });
    if (!written)
        REBOOT_LOG_WARN(Engine, "runtime.json was not updated for pid {}: {}", child.pid, written.error().id);
}

}  // namespace reboot::engine
