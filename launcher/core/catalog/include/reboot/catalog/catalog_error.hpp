#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::catalog {

enum class CatalogErrorCode : u8 {
    // Transport failure; `cause` holds the net diagnostic.
    FetchFailed,
    HttpStatus,
    // Signature or serial check failed; `cause` holds the trust diagnostic.
    Untrusted,
    UnknownSchema,
    Malformed,
    CacheMissing,
    CacheWriteFailed,
    BundledUnusable,
    EntryNotFound,
    EntryNotInstallable,
};

struct CatalogError {
    CatalogErrorCode code = CatalogErrorCode::Malformed;
    std::string url;
    std::optional<NativePath> path;
    u32 http_status = 0;
    u32 schema = 0;
    // The JSON member that failed to parse, such as "entries[3].version".
    std::string where;
    CatalogEntryId entry;
    std::optional<Diagnostic> cause;
};

[[nodiscard]] Diagnostic to_diagnostic(const CatalogError& error);

}  // namespace rb::catalog
