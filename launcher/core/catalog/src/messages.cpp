#include "messages.hpp"

namespace rb::catalog::msg {

REBOOT_MESSAGE(kFetchFailed, "catalog.fetch_failed", "Cannot download the build catalog from {url}");
REBOOT_MESSAGE(kHttpStatus, "catalog.http_status", "The build catalog server returned HTTP {status} for {url}");
REBOOT_MESSAGE(kUntrusted, "catalog.untrusted", "The build catalog failed its signature or serial check");
REBOOT_MESSAGE(kUnknownSchema, "catalog.unknown_schema",
               "The build catalog uses schema {schema}, which this version cannot read");
REBOOT_MESSAGE(kMalformed, "catalog.malformed", "The build catalog is malformed at {where}");
REBOOT_MESSAGE(kCacheMissing, "catalog.cache_missing", "No verified build catalog is cached");
REBOOT_MESSAGE(kCacheWriteFailed, "catalog.cache_write_failed", "Cannot save the build catalog cache to {path}");
REBOOT_MESSAGE(kBundledUnusable, "catalog.bundled_unusable",
               "The build catalog bundled at {path} is unusable; reinstall the launcher");
REBOOT_MESSAGE(kEntryNotFound, "catalog.entry_not_found", "No build named {name} is in the catalog");
REBOOT_MESSAGE(kEntryNotInstallable, "catalog.entry_not_installable", "The build {name} is not available for download");

}  // namespace rb::catalog::msg
