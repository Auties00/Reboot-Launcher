#include "reboot/catalog/bundled_catalog_source.hpp"

#include <string_view>
#include <utility>

#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/ports/file_system.hpp"
#include "verified_catalog.hpp"

namespace reboot::catalog {

namespace {

[[nodiscard]] NativePath signature_of(const NativePath& body) {
    NativePath signature = body;
    signature += ".sig";
    return signature;
}

}  // namespace

BundledCatalogSource::BundledCatalogSource(const InstallLayout& install, ports::IFileSystem& files,
                                           WorkerPool& workers, Executor& strand, const trust::KeyRing& keys)
    : body_(install.bundled_catalog),
      signature_(signature_of(install.bundled_catalog)),
      files_(files),
      workers_(workers),
      strand_(strand),
      keys_(keys) {}

void BundledCatalogSource::load(CatalogFetch, CancelToken token, UniqueFunction<void(CatalogLoadResult)> done) {
    const CatalogError unusable{.code = CatalogErrorCode::BundledUnusable, .path = body_};
    submit_catalog_work<LoadedCatalog>(
        workers_, strand_, std::move(token),
        [&files = files_, &keys = keys_, body_path = body_, signature_path = signature_, unusable]() -> CatalogLoadResult {
            const auto fail = [&](Diagnostic cause) {
                CatalogError error = unusable;
                error.cause = std::move(cause);
                return std::unexpected(std::move(error));
            };
            auto body = files.read_all(body_path);
            if (!body) return fail(std::move(body.error()));
            auto signature = files.read_all(signature_path);
            if (!signature) return fail(std::move(signature.error()));
            const std::string_view signature_text(reinterpret_cast<const char*>(signature->data()), signature->size());
            auto catalog = verify_and_parse(keys, std::move(*body), signature_text);
            if (!catalog) return fail(to_diagnostic(catalog.error()));
            return LoadedCatalog{.catalog = std::move(*catalog), .origin = CatalogOrigin::Bundled, .warnings = {}};
        },
        unusable, std::move(done));
}

}  // namespace reboot::catalog
