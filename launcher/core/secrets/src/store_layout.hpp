#pragma once

#include <cstddef>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/base/types.hpp"
#include "reboot/secrets/secret_kind.hpp"
#include "reboot/secrets/secret_target.hpp"

namespace rb::secrets::detail {

// Kinds a Remember put may write to the store.
[[nodiscard]] constexpr bool storable(SecretKind kind) noexcept { return retention_allowed(kind, Retention::Remember); }

// Windows Credential Manager holds at most 2560 bytes a value, so the index is split into chunks of
// whole lines of at most this many bytes.
inline constexpr std::size_t kIndexChunkBytes = 2048;

// ISecretStore cannot enumerate, so index values list the stored targets for start() to load.
class StoreLayout {
public:
    explicit StoreLayout(std::string_view root_hash16);

    // "<root>/index" for chunk 0, then "<root>/index.1", "<root>/index.2" and so on.
    [[nodiscard]] std::string index_key(std::size_t chunk) const;
    [[nodiscard]] std::string value_key(const SecretTarget& target) const;

private:
    std::string prefix_;
};

// One "<kind name>/<scope>" line per target, in chunks of at most kIndexChunkBytes; none for no targets.
[[nodiscard]] std::vector<std::vector<u8>> encode_index(const std::set<SecretTarget>& targets);
// One chunk; lines that do not name a storable target are skipped.
[[nodiscard]] std::set<SecretTarget> decode_index(std::span<const u8> bytes);

}  // namespace rb::secrets::detail
