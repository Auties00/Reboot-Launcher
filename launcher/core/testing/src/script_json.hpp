#pragma once

// The JSON form of the fake scripts, generated from the structs themselves: keys are field names,
// durations take an _ms suffix, byte arrays are hex, enums and value types are their text forms,
// and a variant is {"<alternative>": {fields}}, or a bare "<alternative>" without fields.

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <boost/json.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/foundation/types.hpp"
#include "wire/codec.hpp"

namespace rb::testing::script_json {

namespace json = boost::json;

// Enums name their enumerators through enum_names(Names, ...) and variant alternatives their key
// through variant_key(Names, ...); the tag lets lookup find overloads declared after this header.
struct Names {};

template <class E>
concept NamedEnum = std::is_enum_v<E> && requires {
    { enum_names(Names{}, std::type_identity<E>{}) } -> std::convertible_to<std::span<const std::string_view>>;
};

template <class T>
struct is_optional : std::false_type {};
template <class T>
struct is_optional<std::optional<T>> : std::true_type {};

template <class T>
struct is_vector : std::false_type {};
template <class T, class A>
struct is_vector<std::vector<T, A>> : std::true_type {};

template <class T>
struct is_variant : std::false_type {};
template <class... T>
struct is_variant<std::variant<T...>> : std::true_type {};

template <class T>
struct is_byte_array : std::false_type {};
template <std::size_t N>
struct is_byte_array<std::array<u8, N>> : std::true_type {};

template <class T>
concept Struct = std::is_class_v<T> && std::is_aggregate_v<T> && !is_byte_array<T>::value && !is_optional<T>::value &&
                 !is_vector<T>::value && !is_variant<T>::value && !std::same_as<T, std::string> && !std::same_as<T, Uuid>;

// The JSON key of field I of T.
template <class T, std::size_t I>
[[nodiscard]] std::string key_of() {
    using Field = std::remove_cvref_t<decltype(sb::wire::field<I>(std::declval<T&>()))>;
    std::string key(sb::wire::field_name<T, I>());
    if constexpr (std::same_as<Field, std::chrono::milliseconds> || std::same_as<Field, std::optional<std::chrono::milliseconds>>)
        key += "_ms";
    return key;
}

// --- writing -------------------------------------------------------------------------------------

template <class T>
[[nodiscard]] json::value write(const T& value);

template <Struct T>
[[nodiscard]] json::object write_struct(const T& value) {
    json::object out;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (
            [&] {
                const auto& field = sb::wire::field<I>(value);
                using Field = std::remove_cvref_t<decltype(field)>;
                if constexpr (is_optional<Field>::value) {
                    if (!field) return;
                }
                out[key_of<T, I>()] = write(field);
            }(),
            ...);
    }(std::make_index_sequence<sb::wire::field_count<T>>{});
    return out;
}

template <class T>
json::value write(const T& value) {
    if constexpr (std::same_as<T, bool>) {
        return json::value(value);
    } else if constexpr (NamedEnum<T>) {
        const std::span<const std::string_view> names = enum_names(Names{}, std::type_identity<T>{});
        const auto index = static_cast<std::size_t>(value);
        return json::value(json::string(index < names.size() ? names[index] : std::string_view("unknown")));
    } else if constexpr (std::signed_integral<T>) {
        return json::value(static_cast<std::int64_t>(value));
    } else if constexpr (std::unsigned_integral<T>) {
        return json::value(static_cast<std::uint64_t>(value));
    } else if constexpr (std::same_as<T, std::string>) {
        return json::value(json::string(value));
    } else if constexpr (std::same_as<T, std::chrono::milliseconds>) {
        return json::value(static_cast<std::int64_t>(value.count()));
    } else if constexpr (is_byte_array<T>::value) {
        return json::value(json::string(to_hex(value)));
    } else if constexpr (std::same_as<T, Uuid>) {
        return json::value(json::string(format_uuid(value)));
    } else if constexpr (is_optional<T>::value) {
        return value ? write(*value) : json::value(nullptr);
    } else if constexpr (is_vector<T>::value) {
        json::array out;
        for (const auto& item : value) out.push_back(write(item));
        return out;
    } else if constexpr (is_variant<T>::value) {
        return std::visit(
            []<class A>(const A& alternative) -> json::value {
                const std::string_view key = variant_key(Names{}, std::type_identity<A>{});
                if constexpr (sb::wire::field_count<A> == 0) {
                    return json::value(json::string(key));
                } else {
                    json::object out;
                    out[key] = write_struct(alternative);
                    return out;
                }
            },
            value);
    } else {
        static_assert(Struct<T>, "a script field of an unsupported type");
        return write_struct(value);
    }
}

// --- reading -------------------------------------------------------------------------------------

// The first problem found, with where it was.
struct ReadError {
    std::string reason;
};

template <class T>
[[nodiscard]] std::optional<ReadError> read(const json::value& in, T& out, const std::string& where);

[[nodiscard]] inline ReadError expected(std::string_view what, const std::string& where) {
    return ReadError{where + ": expected " + std::string(what)};
}

template <std::integral N>
[[nodiscard]] std::optional<N> number_of(const json::value& in) {
    if (in.is_int64()) {
        const std::int64_t v = in.get_int64();
        if constexpr (std::unsigned_integral<N>) {
            if (v < 0 || static_cast<std::uint64_t>(v) > std::numeric_limits<N>::max()) return std::nullopt;
        } else {
            if (v < std::numeric_limits<N>::min() || v > std::numeric_limits<N>::max()) return std::nullopt;
        }
        return static_cast<N>(v);
    }
    if (in.is_uint64()) {
        const std::uint64_t v = in.get_uint64();
        if (v > static_cast<std::uint64_t>(std::numeric_limits<N>::max())) return std::nullopt;
        return static_cast<N>(v);
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<u8> hex_nibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<u8>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<u8>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<u8>(c - 'A' + 10);
    return std::nullopt;
}

template <Struct T>
[[nodiscard]] std::optional<ReadError> read_struct(const json::value& in, T& out, const std::string& where) {
    if (!in.is_object()) return expected("an object", where);
    for (const auto& [key, item] : in.get_object()) {
        std::optional<ReadError> error;
        bool known = false;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (
                [&] {
                    if (known || key != key_of<T, I>()) return;
                    known = true;
                    error = read(item, sb::wire::field<I>(out), where + "." + std::string(key));
                }(),
                ...);
        }(std::make_index_sequence<sb::wire::field_count<T>>{});
        if (!known) return ReadError{where + ": unknown key " + std::string(key)};
        if (error) return error;
    }
    return std::nullopt;
}

template <class Variant, std::size_t... I>
[[nodiscard]] std::optional<ReadError> read_variant(std::string_view key, const json::value* fields, Variant& out,
                                                    const std::string& where, std::index_sequence<I...>) {
    std::optional<ReadError> error;
    bool known = false;
    (
        [&] {
            using A = std::variant_alternative_t<I, Variant>;
            if (known || key != variant_key(Names{}, std::type_identity<A>{})) return;
            known = true;
            A alternative{};
            if (fields != nullptr) {
                error = read_struct(*fields, alternative, where + "." + std::string(key));
            } else if constexpr (sb::wire::field_count<A> != 0) {
                error = expected("fields for " + std::string(key), where);
            }
            if (!error) out = std::move(alternative);
        }(),
        ...);
    if (!known) return ReadError{where + ": unknown alternative " + std::string(key)};
    return error;
}

template <class T>
std::optional<ReadError> read(const json::value& in, T& out, const std::string& where) {
    if constexpr (std::same_as<T, bool>) {
        if (!in.is_bool()) return expected("true or false", where);
        out = in.get_bool();
        return std::nullopt;
    } else if constexpr (NamedEnum<T>) {
        if (!in.is_string()) return expected("a name", where);
        const std::span<const std::string_view> names = enum_names(Names{}, std::type_identity<T>{});
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] != std::string_view(in.get_string())) continue;
            out = static_cast<T>(i);
            return std::nullopt;
        }
        return ReadError{where + ": unknown name " + std::string(in.get_string())};
    } else if constexpr (std::integral<T>) {
        const auto number = number_of<T>(in);
        if (!number) return expected("an integer in range", where);
        out = *number;
        return std::nullopt;
    } else if constexpr (std::same_as<T, std::string>) {
        if (!in.is_string()) return expected("a string", where);
        out = std::string(in.get_string());
        return std::nullopt;
    } else if constexpr (std::same_as<T, std::chrono::milliseconds>) {
        const auto number = number_of<std::int64_t>(in);
        if (!number) return expected("milliseconds", where);
        out = std::chrono::milliseconds{*number};
        return std::nullopt;
    } else if constexpr (is_byte_array<T>::value) {
        if (!in.is_string() || in.get_string().size() != out.size() * 2) return expected("hex bytes", where);
        const std::string_view text = in.get_string();
        for (std::size_t i = 0; i < out.size(); ++i) {
            const auto high = hex_nibble(text[2 * i]);
            const auto low = hex_nibble(text[2 * i + 1]);
            if (!high || !low) return expected("hex bytes", where);
            out[i] = static_cast<u8>(*high << 4 | *low);
        }
        return std::nullopt;
    } else if constexpr (std::same_as<T, Uuid>) {
        if (!in.is_string()) return expected("a UUID", where);
        auto parsed = parse_uuid(in.get_string());
        if (!parsed) return expected("a UUID", where);
        out = *parsed;
        return std::nullopt;
    } else if constexpr (is_optional<T>::value) {
        if (in.is_null()) {
            out.reset();
            return std::nullopt;
        }
        typename T::value_type value{};
        if (auto error = read(in, value, where)) return error;
        out = std::move(value);
        return std::nullopt;
    } else if constexpr (is_vector<T>::value) {
        if (!in.is_array()) return expected("an array", where);
        T items;
        for (std::size_t i = 0; i < in.get_array().size(); ++i) {
            typename T::value_type item{};
            if (auto error = read(in.get_array()[i], item, where + "[" + std::to_string(i) + "]")) return error;
            items.push_back(std::move(item));
        }
        out = std::move(items);
        return std::nullopt;
    } else if constexpr (is_variant<T>::value) {
        constexpr auto alternatives = std::make_index_sequence<std::variant_size_v<T>>{};
        if (in.is_string()) return read_variant(std::string_view(in.get_string()), nullptr, out, where, alternatives);
        if (!in.is_object() || in.get_object().size() != 1) return expected("one alternative", where);
        const auto& entry = *in.get_object().begin();
        return read_variant(std::string_view(entry.key()), &entry.value(), out, where, alternatives);
    } else {
        static_assert(Struct<T>, "a script field of an unsupported type");
        return read_struct(in, out, where);
    }
}

}  // namespace rb::testing::script_json
