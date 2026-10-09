#pragma once

#include <expected>
#include <span>

#include "reboot/catalog/catalog.hpp"
#include "reboot/catalog/catalog_error.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::catalog {

// Parses a body that already passed verify_signed; tests/data/catalog.json shows the layout.
// - A schema other than kCatalogSchema is UnknownSchema. A duplicate id (ASCII case-insensitive),
//   an inverted or overlapping flag range, or an entry version no range covers is Malformed.
// - Unknown keys are ignored. An unknown enum string becomes Unrecognized for a format or
//   container, Unavailable for availability, Unknown for an XMPP note, Withhold for hotfix
//   delivery, and leaves that runner's boot_inject absent.
// - Entries come out sorted by version, then id.
[[nodiscard]] std::expected<Catalog, CatalogError> parse_catalog(std::span<const u8> json);

}  // namespace rb::catalog
