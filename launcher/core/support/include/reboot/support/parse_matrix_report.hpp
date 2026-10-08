#pragma once

#include <span>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/support/evidence_record.hpp"

namespace reboot::support {

inline constexpr u32 kMatrixReportSchema = 1;

// Parses the test-rig matrix report that the release pipeline publishes with each release
// manifest, from a body whose signature was already verified. Each row is one cell run:
// role, os, runtime {id, kind}, range, build, version, cl, the role's inputs, recorded_at (Unix
// seconds), status and log_ref. Only pass and fail rows are runs; untested and blocked(reason)
// rows are reports of the rig and yield no record. Fails with support.matrix_report_unknown_schema
// or support.matrix_report_malformed.
[[nodiscard]] Result<std::vector<EvidenceRecord>> parse_matrix_report(std::span<const u8> json);

}  // namespace reboot::support
