#include "reboot/trust/key_ring.hpp"

#include <span>

#include "reboot/foundation/sha256.hpp"

namespace reboot::trust {

std::string key_id_of(const Ed25519PublicKey& key) {
    const auto digest = sha256(key);
    return to_hex(std::span(digest).first<8>());
}

KeyRing::KeyRing(SignedDocumentKind kind, const PinnedKeys& keys) : kind_(kind) {
    for (const auto& key : {keys.current, keys.next})
        if (key) entries_.push_back(Entry{key_id_of(*key), *key});
}

const Ed25519PublicKey* KeyRing::find(std::string_view key_id) const noexcept {
    for (const auto& entry : entries_)
        if (entry.key_id == key_id) return &entry.key;
    return nullptr;
}

}  // namespace reboot::trust
