#pragma once

#include <optional>
#include <span>
#include <string_view>

#include <boost/json/value.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/key.hpp"
#include "reboot/storage/settings_document.hpp"
#include "reboot/storage/settings_patch.hpp"
#include "reboot/storage/settings_snapshot.hpp"
#include "reboot/storage/settings_values.hpp"

namespace rb {
class EventBus;
}

namespace rb::storage {

class SettingsRegistry;

// Capabilities: settings-storage.layout.
// Strand-only. The only writer of config/settings.json; publishes SettingsChanged for every change.
class Settings {
public:
    Settings(DocumentStore<SettingsDocument>& store, const SettingsRegistry& registry, EventBus& events);
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    [[nodiscard]] SettingsSnapshot snapshot() const;

    // One bad field fails it all with storage.invalid_setting; no change returns the current revision.
    Result<u64> patch(const SettingsPatch& patch);

    // For search results and generic editors; an unknown id fails with storage.unknown_key.
    [[nodiscard]] Result<boost::json::value> get(std::string_view key_id) const;
    Result<u64> set(std::string_view key_id, const boost::json::value& value, std::optional<u64> expected_revision);
    // One revision and one SettingsChanged for all of `keys`.
    Result<u64> reset_to_defaults(std::span<const AnyKey* const> keys);

private:
    Result<u64> commit(const SettingsValues& next);

    DocumentStore<SettingsDocument>& store_;
    const SettingsRegistry& registry_;
    EventBus& events_;
    // What the last SettingsChanged carried, to name the keys a hand edit changed.
    SettingsValues published_;
};

}  // namespace rb::storage
