#include "reboot/catalog/catalog_error.hpp"

#include <utility>

#include "messages.hpp"

namespace rb::catalog {

namespace {

[[nodiscard]] Diagnostic with_cause(DiagBuilder&& builder, const std::optional<Diagnostic>& cause) {
    if (cause) return std::move(builder).cause(*cause).build();
    return std::move(builder).build();
}

[[nodiscard]] bool retryable_status(u32 status) noexcept { return status == 429 || status >= 500; }

}  // namespace

Diagnostic to_diagnostic(const CatalogError& error) {
    const auto diag = [](MessageId message) { return make_diag(ErrorDomain::Catalog, message); };
    const NativePath path = error.path.value_or(NativePath{});
    switch (error.code) {
        case CatalogErrorCode::FetchFailed:
            return with_cause(diag(msg::kFetchFailed).arg("url", error.url).retryable(), error.cause);
        case CatalogErrorCode::HttpStatus:
            return diag(msg::kHttpStatus)
                .arg("status", error.http_status)
                .arg("url", error.url)
                .retryable(retryable_status(error.http_status))
                .build();
        case CatalogErrorCode::Untrusted: return with_cause(diag(msg::kUntrusted), error.cause);
        case CatalogErrorCode::UnknownSchema:
            return diag(msg::kUnknownSchema).arg("schema", error.schema).kind(ErrorKind::Unsupported).build();
        case CatalogErrorCode::Malformed: return diag(msg::kMalformed).arg("where", error.where).build();
        case CatalogErrorCode::CacheMissing:
            return with_cause(diag(msg::kCacheMissing).kind(ErrorKind::NotFound), error.cause);
        case CatalogErrorCode::CacheWriteFailed:
            return with_cause(diag(msg::kCacheWriteFailed).arg("path", path), error.cause);
        case CatalogErrorCode::BundledUnusable:
            return with_cause(diag(msg::kBundledUnusable).arg("path", path), error.cause);
        case CatalogErrorCode::EntryNotFound:
            return diag(msg::kEntryNotFound).arg("name", error.entry).kind(ErrorKind::NotFound).build();
        case CatalogErrorCode::EntryNotInstallable:
            return diag(msg::kEntryNotInstallable).arg("name", error.entry).kind(ErrorKind::Conflict).build();
    }
    return internal_bug("catalog::to_diagnostic");
}

}  // namespace rb::catalog
