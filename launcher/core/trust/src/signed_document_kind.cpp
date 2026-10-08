#include "reboot/trust/signed_document_kind.hpp"

namespace reboot::trust {

std::string_view document_kind_name(SignedDocumentKind kind) noexcept {
    switch (kind) {
        case SignedDocumentKind::BuildCatalog: return "build_catalog";
        case SignedDocumentKind::ReleaseManifest: return "release_manifest";
    }
    return "unknown";
}

std::string_view signature_context(SignedDocumentKind kind) noexcept {
    switch (kind) {
        case SignedDocumentKind::BuildCatalog: return "reboot-launcher/build-catalog/v1\n";
        case SignedDocumentKind::ReleaseManifest: return "reboot-launcher/release-manifest/v1\n";
    }
    return {};
}

}  // namespace reboot::trust
