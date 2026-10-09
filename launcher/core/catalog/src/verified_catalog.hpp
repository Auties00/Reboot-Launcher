#pragma once

#include <expected>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/catalog/catalog.hpp"
#include "reboot/catalog/catalog_error.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::trust {
class KeyRing;
}

namespace rb::catalog {

// Untrusted for a bad signature file or signature, otherwise parse_catalog's error.
[[nodiscard]] std::expected<Catalog, CatalogError> verify_and_parse(const trust::KeyRing& keys, std::vector<u8> body,
                                                                   std::string_view signature_file);

// WorkerPool::submit for work that fails with a CatalogError; an escaped exception arrives as
// `on_bug` with the internal.bug diagnostic as its cause.
template <class T>
void submit_catalog_work(WorkerPool& workers, Executor& strand, CancelToken token,
                         UniqueFunction<std::expected<T, CatalogError>()> work, CatalogError on_bug,
                         UniqueFunction<void(std::expected<T, CatalogError>)> done) {
    using Outcome = std::expected<T, CatalogError>;
    workers.submit<Outcome>(
        [work = std::move(work)](CancelToken) mutable -> Result<Outcome> { return work(); }, std::move(token), strand,
        [on_bug = std::move(on_bug), done = std::move(done)](Result<Outcome> result) mutable {
            if (result) return done(std::move(*result));
            on_bug.cause = std::move(result.error());
            done(std::unexpected(std::move(on_bug)));
        });
}

}  // namespace rb::catalog
