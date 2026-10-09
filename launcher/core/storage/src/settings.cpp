#include "reboot/storage/settings.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/storage/settings_changed.hpp"
#include "reboot/storage/settings_keys.hpp"
#include "reboot/storage/settings_registry.hpp"

namespace rb::storage {

namespace {

[[nodiscard]] Diagnostic invalid_setting(const AnyKey& key, Diagnostic cause) {
    return invalid_input(msg::kInvalidSetting).arg("key", key.spec().id).cause(std::move(cause)).build();
}

template <class T>
[[nodiscard]] Result<void> apply(const Key<T>& key, const std::optional<T>& value, SettingsValues& next) {
    if (!value) return {};
    Result<void> stored = key.assign(next, *value);
    if (!stored) return std::unexpected(invalid_setting(key, std::move(stored.error())));
    return {};
}

[[nodiscard]] Result<void> check_revision(u64 current, std::optional<u64> expected) {
    if (!expected || *expected == current) return {};
    return make_diag(ErrorDomain::Storage, msg::kRevisionConflict)
        .kind(ErrorKind::Conflict)
        .arg("current", current)
        .arg("expected", *expected)
        .fail();
}

[[nodiscard]] std::size_t address_bytes(const HostPort& endpoint) noexcept { return endpoint.host.size(); }

}  // namespace

std::size_t SettingsChanged::approx_bytes() const noexcept {
    std::size_t bytes = sizeof(SettingsChanged);
    for (const std::string& key : keys) bytes += sizeof(std::string) + key.size();
    bytes += values.client.game_culture.size() + values.play.custom_args.size() + values.play.env.size() +
             values.ui.language.size();
    if (values.play.custom_auth_dll)
        bytes += values.play.custom_auth_dll->native().size() * sizeof(NativePath::value_type);
    bytes += values.backend.console_key.name.size() + address_bytes(values.backend.target.local.endpoint);
    if (values.backend.target.remote) bytes += address_bytes(values.backend.target.remote->endpoint);
    return bytes;
}

Settings::Settings(DocumentStore<SettingsDocument>& store, const SettingsRegistry& registry, EventBus& events)
    : store_(store), registry_(registry), events_(events), published_(store.get().values) {
    store_.set_on_reload([this](const SettingsDocument& document, std::span<const ValueIssue>) {
        std::vector<std::string> keys;
        for (const AnyKey* key : registry_.all())
            if (key->differs(published_, document.values)) keys.emplace_back(key->spec().id);
        if (keys.empty()) return;
        published_ = document.values;
        events_.publish(EventKind::SettingsChanged, SettingsChanged{store_.revision(), std::move(keys), published_});
    });
}

SettingsSnapshot Settings::snapshot() const {
    return SettingsSnapshot{.revision = store_.revision(), .mode = store_.mode(), .values = store_.get().values};
}

Result<u64> Settings::patch(const SettingsPatch& patch) {
    if (Result<void> current = check_revision(store_.revision(), patch.expected_revision); !current)
        return std::unexpected(std::move(current.error()));

    SettingsValues next = store_.get().values;
    const std::array<Result<void>, keys::kAll.size()> applied{
        apply(keys::kClientGameCulture, patch.client.game_culture, next),
        apply(keys::kPlayCustomArgs, patch.play.custom_args, next),
        apply(keys::kPlayCustomAuthDll, patch.play.custom_auth_dll, next),
        apply(keys::kPlayEnv, patch.play.env, next),
        apply(keys::kPlayVerboseWineLog, patch.play.verbose_wine_log, next),
        apply(keys::kBackendTarget, patch.backend.target, next),
        apply(keys::kBackendAllowLan, patch.backend.allow_lan, next),
        apply(keys::kBackendConsoleKey, patch.backend.console_key, next),
        apply(keys::kHostUpdatePolicy, patch.host.update_policy, next),
        apply(keys::kHostListing, patch.host.listing, next),
        apply(keys::kUpdatesChannel, patch.updates.channel, next),
        apply(keys::kUpdatesAutoCheck, patch.updates.auto_check, next),
        apply(keys::kUiLanguage, patch.ui.language, next),
        apply(keys::kUiTheme, patch.ui.theme, next),
    };
    for (const Result<void>& result : applied)
        if (!result) return std::unexpected(result.error());
    return commit(next);
}

Result<boost::json::value> Settings::get(std::string_view key_id) const {
    const AnyKey* key = registry_.find(key_id);
    if (key == nullptr)
        return make_diag(ErrorDomain::Storage, msg::kUnknownKey).kind(ErrorKind::NotFound).arg("key", key_id).fail();
    return key->encode(store_.get().values);
}

Result<u64> Settings::set(std::string_view key_id, const boost::json::value& value,
                          std::optional<u64> expected_revision) {
    const AnyKey* key = registry_.find(key_id);
    if (key == nullptr)
        return make_diag(ErrorDomain::Storage, msg::kUnknownKey).kind(ErrorKind::NotFound).arg("key", key_id).fail();
    if (Result<void> current = check_revision(store_.revision(), expected_revision); !current)
        return std::unexpected(std::move(current.error()));

    SettingsValues next = store_.get().values;
    Result<void> stored = key->decode_into(next, value);
    if (!stored) return std::unexpected(invalid_setting(*key, std::move(stored.error())));
    return commit(next);
}

Result<u64> Settings::reset_to_defaults(std::span<const AnyKey* const> keys) {
    SettingsValues next = store_.get().values;
    for (const AnyKey* key : keys) key->reset(next);
    return commit(next);
}

Result<u64> Settings::commit(const SettingsValues& next) {
    std::vector<std::string> changed;
    for (const AnyKey* key : registry_.all())
        if (key->differs(store_.get().values, next)) changed.emplace_back(key->spec().id);
    if (changed.empty()) return store_.revision();

    Result<u64> revision = store_.update([&next](SettingsDocument& document) { document.values = next; });
    if (!revision) return revision;
    published_ = next;
    events_.publish(EventKind::SettingsChanged, SettingsChanged{*revision, std::move(changed), published_});
    return revision;
}

}  // namespace rb::storage
