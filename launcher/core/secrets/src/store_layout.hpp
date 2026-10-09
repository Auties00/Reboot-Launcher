#pragma once

#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/types.hpp"
#include "reboot/secrets/secret_kind.hpp"
#include "reboot/secrets/secret_target.hpp"

namespace rb::secrets::detail {

// Kinds a Remember put may write to the store.
[[nodiscard]] constexpr bool storable(SecretKind kind) noexcept { return retention_allowed(kind, Retention::Remember); }

// ISecretStore cannot enumerate, so an index value lists the stored targets for start() to load.
class StoreLayout {
public:
    explicit StoreLayout(std::string_view root_hash16);

    [[nodiscard]] const std::string& index_key() const noexcept { return index_key_; }
    [[nodiscard]] std::string value_key(const SecretTarget& target) const;

private:
    std::string prefix_;
    std::string index_key_;
};

// One "<kind name>/<scope>" line per target.
[[nodiscard]] std::vector<u8> encode_index(const std::set<SecretTarget>& targets);
// Lines that do not name a storable target are skipped.
[[nodiscard]] std::set<SecretTarget> decode_index(std::span<const u8> bytes);

}  // namespace rb::secrets::detail
