#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <boost/json/value.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/backend_target.hpp"
#include "reboot/storage/console_key.hpp"
#include "reboot/storage/enum_names.hpp"
#include "reboot/storage/json_values.hpp"
#include "reboot/storage/settings_values.hpp"

namespace rb::storage {

// The Play, Host and Backend tabs' reset buttons.
enum class ResetGroup : u8 { Play, Host, Backend };

// Lets callers that only have a key id (search, reset, generic editors) pick an editor.
enum class ValueKind : u8 { Bool, Text, OptionalPath, Choice, ConsoleKey, BackendTarget };

struct KeySpec {
    // Dotted and stable, e.g. "backend.allow_lan"; also the member name in settings.json.
    std::string_view id;
    // What settings search matches and UIs show.
    MessageId label;
    Sensitivity sensitivity = Sensitivity::Public;
    std::optional<ResetGroup> reset_group;
    // Filled in by Key<T> from its value type.
    ValueKind kind = ValueKind::Text;
    std::span<const std::string_view> choices;
};

// Decoding checks the JSON shape only; Key<T> validates.
template <class T>
struct SettingCodec;

template <>
struct SettingCodec<bool> {
    static constexpr ValueKind kKind = ValueKind::Bool;
    [[nodiscard]] static boost::json::value encode(bool value);
    [[nodiscard]] static Result<bool> decode(const boost::json::value& raw);
};

template <>
struct SettingCodec<std::string> {
    static constexpr ValueKind kKind = ValueKind::Text;
    [[nodiscard]] static boost::json::value encode(const std::string& value);
    [[nodiscard]] static Result<std::string> decode(const boost::json::value& raw);
};

// null when unset.
template <>
struct SettingCodec<std::optional<NativePath>> {
    static constexpr ValueKind kKind = ValueKind::OptionalPath;
    [[nodiscard]] static boost::json::value encode(const std::optional<NativePath>& value);
    [[nodiscard]] static Result<std::optional<NativePath>> decode(const boost::json::value& raw);
};

template <>
struct SettingCodec<ConsoleKey> {
    static constexpr ValueKind kKind = ValueKind::ConsoleKey;
    [[nodiscard]] static boost::json::value encode(const ConsoleKey& value);
    [[nodiscard]] static Result<ConsoleKey> decode(const boost::json::value& raw);
};

// {"kind": "remote", "local": {"host", "port", "xmpp"?}, "remote"?: {"scheme"?, "host", "port", "xmpp"?}}.
template <>
struct SettingCodec<BackendTarget> {
    static constexpr ValueKind kKind = ValueKind::BackendTarget;
    [[nodiscard]] static boost::json::value encode(const BackendTarget& value);
    [[nodiscard]] static Result<BackendTarget> decode(const boost::json::value& raw);
};

template <PersistedEnum E>
struct SettingCodec<E> {
    static constexpr ValueKind kKind = ValueKind::Choice;
    [[nodiscard]] static boost::json::value encode(E value) { return enum_to_json(value); }
    [[nodiscard]] static Result<E> decode(const boost::json::value& raw) { return enum_from_json<E>(raw); }
};

// Where a key's value lives in SettingsValues.
template <class T>
struct SettingsField {
    T& (*get)(SettingsValues&) noexcept;
    const T& (*get_const)(const SettingsValues&) noexcept;
};

template <auto Group, auto Member>
[[nodiscard]] constexpr auto settings_field() noexcept {
    using T = std::remove_cvref_t<decltype((std::declval<SettingsValues&>().*Group).*Member)>;
    return SettingsField<T>{[](SettingsValues& values) noexcept -> T& { return (values.*Group).*Member; },
                            [](const SettingsValues& values) noexcept -> const T& { return (values.*Group).*Member; }};
}

class AnyKey {
public:
    constexpr virtual ~AnyKey() = default;

    [[nodiscard]] constexpr virtual const KeySpec& spec() const noexcept = 0;
    [[nodiscard]] virtual boost::json::value encode(const SettingsValues& values) const = 0;
    // Stores `raw` in `values` if it decodes and validates; otherwise leaves `values` alone.
    [[nodiscard]] virtual Result<void> decode_into(SettingsValues& values, const boost::json::value& raw) const = 0;
    virtual void reset(SettingsValues& values) const = 0;
    [[nodiscard]] virtual bool differs(const SettingsValues& a, const SettingsValues& b) const = 0;

    [[nodiscard]] boost::json::value default_json() const { return encode(SettingsValues{}); }
};

template <class T>
class Key final : public AnyKey {
public:
    // Normalises a value or rejects it; on load a rejected value becomes the default.
    using Validator = Result<T> (*)(T value);

    constexpr Key(KeySpec spec, SettingsField<T> field, Validator validator = nullptr) noexcept
        : spec_(spec), field_(field), validator_(validator) {
        spec_.kind = SettingCodec<T>::kKind;
        if constexpr (PersistedEnum<T>) spec_.choices = EnumNames<T>::kNames;
    }

    [[nodiscard]] constexpr const KeySpec& spec() const noexcept override { return spec_; }
    [[nodiscard]] const T& get(const SettingsValues& values) const noexcept { return field_.get_const(values); }
    [[nodiscard]] T default_value() const {
        const SettingsValues defaults;
        return get(defaults);
    }

    [[nodiscard]] Result<T> validate(T value) const {
        if (validator_ == nullptr) return value;
        return validator_(std::move(value));
    }
    [[nodiscard]] Result<void> assign(SettingsValues& values, T value) const {
        return validate(std::move(value)).transform([&](T valid) { field_.get(values) = std::move(valid); });
    }

    [[nodiscard]] boost::json::value encode(const SettingsValues& values) const override {
        return SettingCodec<T>::encode(get(values));
    }
    [[nodiscard]] Result<void> decode_into(SettingsValues& values, const boost::json::value& raw) const override {
        return SettingCodec<T>::decode(raw).and_then([&](T value) { return assign(values, std::move(value)); });
    }
    void reset(SettingsValues& values) const override { field_.get(values) = default_value(); }
    [[nodiscard]] bool differs(const SettingsValues& a, const SettingsValues& b) const override {
        return !(get(a) == get(b));
    }

private:
    KeySpec spec_;
    SettingsField<T> field_;
    Validator validator_;
};

}  // namespace rb::storage
