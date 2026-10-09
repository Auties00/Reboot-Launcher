#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::ux {

enum class CatalogIssueKind : u8 {
    MissingId,
    // A core-owned key that no linked REBOOT_MESSAGE declares any more.
    OrphanedId,
    PlaceholderMismatch,
    // Only English one/other plurals are allowed in v1.
    UnsupportedPlural,
    MalformedTemplate,
    // An id the registry declares more than once with different English templates.
    ConflictingDeclarations,
};

struct CatalogIssue {
    CatalogIssueKind kind{};
    std::string id;
    std::string detail;
};

// A key is core-owned when its prefix before the first '.' is a domain_prefix(); every other key is UI-owned.
[[nodiscard]] bool is_core_owned_key(std::string_view key);

// Capabilities: localization.strings-and-language, localization.+37.
// Keeps l10n/en.json in step with the message registry; the CI gate runs check() over every package.
class MessageCatalogExport {
public:
    explicit MessageCatalogExport(std::span<const MessageSpec* const> registry) : registry_(registry) {}

    // The new l10n/en.json: registry templates, UI-owned keys of `existing_catalog` kept, orphans dropped.
    [[nodiscard]] Result<std::string> render(std::optional<std::string_view> existing_catalog) const;

    // Every registry id present and declared alike, no orphans, no empty templates, placeholders equal by name,
    // plurals limited to one/other.
    [[nodiscard]] Result<std::vector<CatalogIssue>> check(std::string_view catalog_json) const;

private:
    std::span<const MessageSpec* const> registry_;
};

}  // namespace reboot::ux
