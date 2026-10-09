#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "reboot/storage/key.hpp"
#include "reboot/storage/settings_keys.hpp"

namespace rb::storage {

// Capabilities: none.
// Key lookup by id and reset group; immutable, so any thread may read it.
class SettingsRegistry {
public:
    explicit SettingsRegistry(std::span<const AnyKey* const> keys = keys::kAll) noexcept : keys_(keys) {}

    [[nodiscard]] std::span<const AnyKey* const> all() const noexcept { return keys_; }
    [[nodiscard]] const AnyKey* find(std::string_view id) const noexcept;
    [[nodiscard]] std::vector<const AnyKey*> in_group(ResetGroup group) const;

private:
    std::span<const AnyKey* const> keys_;
};

}  // namespace rb::storage
