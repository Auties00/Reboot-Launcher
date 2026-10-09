#include "reboot/storage/settings_registry.hpp"

#include <algorithm>

namespace rb::storage {

const AnyKey* SettingsRegistry::find(std::string_view id) const noexcept {
    const auto found = std::ranges::find(keys_, id, [](const AnyKey* key) { return key->spec().id; });
    return found == keys_.end() ? nullptr : *found;
}

std::vector<const AnyKey*> SettingsRegistry::in_group(ResetGroup group) const {
    std::vector<const AnyKey*> out;
    for (const AnyKey* key : keys_)
        if (key->spec().reset_group == group) out.push_back(key);
    return out;
}

}  // namespace rb::storage
