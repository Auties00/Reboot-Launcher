#include "verified_catalog.hpp"

#include "reboot/catalog/parse_catalog.hpp"
#include "reboot/trust/key_ring.hpp"
#include "reboot/trust/signed_document.hpp"
#include "reboot/trust/verify_signed.hpp"

namespace rb::catalog {

std::expected<Catalog, CatalogError> verify_and_parse(const trust::KeyRing& keys, std::vector<u8> body,
                                                      std::string_view signature_file) {
    const auto untrusted = [](const trust::TrustError& error) {
        return std::unexpected(CatalogError{.code = CatalogErrorCode::Untrusted, .cause = trust::to_diagnostic(error)});
    };
    auto document = trust::make_signed_document(trust::SignedDocumentKind::BuildCatalog, std::move(body), signature_file);
    if (!document) return untrusted(document.error());
    if (auto verified = trust::verify_signed(keys, *document); !verified) return untrusted(verified.error());
    return parse_catalog(document->body);
}

}  // namespace rb::catalog
