#pragma once

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/runtime_document.hpp"

namespace rb::process {

// The state/runtime.json types themselves, so the two cannot drift.
using ChildRole = storage::ChildRole;
// The supervisor leaves `ports` empty; an owner with listeners wraps its ChildRecordCallback to add them.
using ChildRecord = storage::RecordedProcess;

enum class RecordChange : u8 { Spawned, Exited };

// The engine applies each change to RuntimeDocument and rewrites state/runtime.json.
using ChildRecordCallback = UniqueFunction<void(const ChildRecord&, RecordChange)>;

}  // namespace rb::process
