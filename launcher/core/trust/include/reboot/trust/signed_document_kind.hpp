#pragma once

#include <string_view>

#include "reboot/foundation/types.hpp"

namespace reboot::trust {

enum class SignedDocumentKind : u8 { BuildCatalog, ReleaseManifest };

[[nodiscard]] std::string_view document_kind_name(SignedDocumentKind kind) noexcept;

// The signature covers signature_context(kind) followed by the body, so a key that signs
// both kinds can never have a catalog accepted as a manifest.
[[nodiscard]] std::string_view signature_context(SignedDocumentKind kind) noexcept;

}  // namespace reboot::trust
