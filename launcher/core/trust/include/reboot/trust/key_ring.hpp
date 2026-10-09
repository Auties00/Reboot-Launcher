#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/types.hpp"
#include "reboot/trust/signed_document_kind.hpp"

namespace rb::trust {

inline constexpr std::size_t kEd25519PublicKeySize = 32;

using Ed25519PublicKey = std::array<u8, kEd25519PublicKeySize>;

// First 8 bytes of sha256(key) in lowercase hex; derived, so a .sig can never name a key
// under the wrong id.
[[nodiscard]] std::string key_id_of(const Ed25519PublicKey& key);

// Two slots so a key rotation ships "next" one release before it starts signing.
struct PinnedKeys {
    std::optional<Ed25519PublicKey> current;
    std::optional<Ed25519PublicKey> next;
};

// Signer keys for one document kind. Decisions: archive-extraction-download (catalog),
// release-pipeline and update-mechanism (manifest); no capability id is assigned to trust.
class KeyRing {
public:
    KeyRing(SignedDocumentKind kind, const PinnedKeys& keys);

    // The keys compiled into this build. An unconfigured build pins none and rejects every
    // document with UnknownKey.
    [[nodiscard]] static KeyRing pinned(SignedDocumentKind kind);

    [[nodiscard]] SignedDocumentKind kind() const noexcept { return kind_; }
    [[nodiscard]] const Ed25519PublicKey* find(std::string_view key_id) const noexcept;

private:
    struct Entry {
        std::string key_id;
        Ed25519PublicKey key{};
    };

    SignedDocumentKind kind_;
    std::vector<Entry> entries_;
};

}  // namespace rb::trust
