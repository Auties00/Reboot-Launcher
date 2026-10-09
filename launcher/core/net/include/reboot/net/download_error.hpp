#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::net {

enum class DownloadErrorCode : u8 {
    InsufficientSpace,
    // A non-206 reply, or a Content-Range that does not start at the offset asked for.
    RangeNotHonored,
    // The If-Range validator or the total size no longer matches the sidecar.
    SourceChanged,
    SizeMismatch,
    HttpStatus,
    Io,
    AttemptsExhausted,
    Cancelled,
};

struct DownloadError {
    DownloadErrorCode code = DownloadErrorCode::Io;
    std::string host;
    NativePath file;
    u64 offset = 0;
    std::optional<u32> status;
    std::optional<u64> needed_bytes;
    std::optional<u64> free_bytes;
    std::optional<u64> expected_bytes;
    u32 attempts = 0;
    std::optional<Diagnostic> cause;
};

[[nodiscard]] Diagnostic to_diagnostic(const DownloadError& error);

}  // namespace reboot::net
