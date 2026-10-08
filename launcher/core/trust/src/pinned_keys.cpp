#include <optional>
#include <string_view>

#include "hex.hpp"
#include "reboot/trust/key_ring.hpp"

#if !defined(REBOOT_TRUST_CATALOG_KEY_CURRENT) || !defined(REBOOT_TRUST_CATALOG_KEY_NEXT) || \
    !defined(REBOOT_TRUST_MANIFEST_KEY_CURRENT) || !defined(REBOOT_TRUST_MANIFEST_KEY_NEXT)
#error "core/trust/CMakeLists.txt defines the pinned public keys"
#endif

namespace reboot::trust {

namespace {

[[nodiscard]] constexpr bool valid_pin(std::string_view hex) {
    return hex.empty() || decode_hex<kEd25519PublicKeySize>(hex).has_value();
}

[[nodiscard]] constexpr std::optional<Ed25519PublicKey> pin(std::string_view hex) {
    if (hex.empty()) return std::nullopt;
    return decode_hex<kEd25519PublicKeySize>(hex);
}

constexpr std::string_view kCatalogCurrent = REBOOT_TRUST_CATALOG_KEY_CURRENT;
constexpr std::string_view kCatalogNext = REBOOT_TRUST_CATALOG_KEY_NEXT;
constexpr std::string_view kManifestCurrent = REBOOT_TRUST_MANIFEST_KEY_CURRENT;
constexpr std::string_view kManifestNext = REBOOT_TRUST_MANIFEST_KEY_NEXT;

static_assert(valid_pin(kCatalogCurrent) && valid_pin(kCatalogNext) && valid_pin(kManifestCurrent) &&
                  valid_pin(kManifestNext),
              "a pinned key must be empty or 64 hex digits");

}  // namespace

KeyRing KeyRing::pinned(SignedDocumentKind kind) {
    switch (kind) {
        case SignedDocumentKind::BuildCatalog: return KeyRing(kind, {pin(kCatalogCurrent), pin(kCatalogNext)});
        case SignedDocumentKind::ReleaseManifest: return KeyRing(kind, {pin(kManifestCurrent), pin(kManifestNext)});
    }
    return KeyRing(kind, {});
}

}  // namespace reboot::trust
