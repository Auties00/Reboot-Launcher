#pragma once

#include <cstddef>
#include <vector>

#include "reboot/base/diag.hpp"
#include "reboot/base/types.hpp"
#include "reboot/catalog/catalog_source.hpp"

namespace rb::catalog {

// Published as EventKind::CatalogChanged when the active catalog changes, and also the result
// of a refresh op, which describes the active catalog afterwards.
struct CatalogUpdated {
    u64 serial = 0;
    CatalogOrigin origin = CatalogOrigin::Bundled;
    std::size_t entries = 0;
    std::size_t installable = 0;
    // Why a fallback copy is active, and expiry warnings; the active copy stays in use.
    std::vector<Diagnostic> warnings;
};

}  // namespace rb::catalog
