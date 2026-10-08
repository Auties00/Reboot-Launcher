#pragma once

#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/process/child_record.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/runtime_document.hpp"

namespace reboot::engine {

struct EngineInfo;

// Capabilities: none. Strand-only; keeps state/runtime.json in step, so the next start can reap
// what a crash left.
class RuntimeRecorder {
public:
    explicit RuntimeRecorder(storage::DocumentStore<storage::RuntimeDocument>& document) noexcept
        : document_(document) {}
    RuntimeRecorder(const RuntimeRecorder&) = delete;
    RuntimeRecorder& operator=(const RuntimeRecorder&) = delete;

    // Step 5's input: what the previous engine left recorded.
    [[nodiscard]] std::vector<process::ChildRecord> recorded_children() const;
    // After step 5: drops the entries the reaper killed or found gone.
    Result<void> forget(const std::vector<process::ChildRecord>& reaped);

    // Step 11.
    Result<void> write_started(const EngineInfo& info);
    Result<void> set_ports(std::vector<storage::EnginePort> ports);

    [[nodiscard]] process::ChildRecordCallback recorder();
    void record(const process::ChildRecord& child, process::RecordChange change);

private:
    storage::DocumentStore<storage::RuntimeDocument>& document_;
};

}  // namespace reboot::engine
