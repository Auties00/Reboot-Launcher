#pragma once

// Protobuf-wire-compatible codec generated from plain aggregates.
// Field number = declaration index + 1. Supported member types:
//   bool, unsigned ints, enums      -> varint          (proto: bool/uint32/uint64/enum)
//   signed ints                     -> zigzag varint   (proto: sint32/sint64)
//   std::string, Bytes              -> length-delimited (proto: string/bytes)
//   std::array<u8, N>, Uuid         -> length-delimited, exactly N bytes (proto: bytes)
//   nested aggregate                -> length-delimited message, always emitted
//   std::optional<T>                -> explicit presence (proto3 `optional`)
//   std::vector<scalar>             -> packed           (proto: repeated scalar)
//   std::vector<string|msg>         -> repeated length-delimited
// Defaults (0, false, empty) are omitted, matching canonical proto3 output byte for byte.
// Member access uses P2996 reflection when the compiler has it, Boost.PFR otherwise.

#include <array>
#include <concepts>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/features.hpp"
#include "core/types.hpp"
#include "wire/buffer.hpp"

#if !SB_HAS_REFLECTION
#include <boost/pfr.hpp>
#endif

namespace sb::wire {

using Bytes = std::vector<u8>;

enum class WireType : u8 { varint = 0, i64 = 1, len = 2, i32 = 5 };

// ---- member access ---------------------------------------------------------------------------

#if SB_HAS_REFLECTION
namespace refl {
template <class T>
consteval auto members() {
    return std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
}
}  // namespace refl

template <class T>
inline constexpr std::size_t field_count = refl::members<T>().size();

template <std::size_t I, class T>
constexpr decltype(auto) field(T& obj) noexcept {
    constexpr auto m = refl::members<std::remove_const_t<T>>()[I];
    return (obj.[:m:]);
}

template <class T, std::size_t I>
consteval std::string_view field_name() {
    return std::meta::identifier_of(refl::members<T>()[I]);
}
#else
template <class T>
inline constexpr std::size_t field_count = boost::pfr::tuple_size_v<T>;

template <std::size_t I, class T>
constexpr decltype(auto) field(T& obj) noexcept {
    return boost::pfr::get<I>(obj);
}

template <class T, std::size_t I>
constexpr std::string_view field_name() {
    return boost::pfr::get_name<I, T>();
}
#endif

// ---- type classification ---------------------------------------------------------------------

template <class T>
struct is_optional : std::false_type {};
template <class T>
struct is_optional<std::optional<T>> : std::true_type {};

template <class T>
struct is_vector : std::false_type {};
template <class T, class A>
struct is_vector<std::vector<T, A>> : std::true_type {};

template <class T>
struct is_byte_array : std::false_type {};
template <std::size_t N>
struct is_byte_array<std::array<u8, N>> : std::true_type {};

template <class T>
concept VarintScalar = std::same_as<T, bool> || std::unsigned_integral<T> || std::is_enum_v<T>;
template <class T>
concept SignedScalar = std::signed_integral<T> && !std::same_as<T, bool>;
template <class T>
concept Scalar = VarintScalar<T> || SignedScalar<T>;
template <class T>
concept FixedBytes = is_byte_array<T>::value || std::same_as<T, Uuid>;
template <class T>
concept LenScalar = std::same_as<T, std::string> || std::same_as<T, Bytes> || FixedBytes<T>;
template <class T>
concept Message = std::is_class_v<T> && std::is_aggregate_v<T> && !LenScalar<T> && !is_optional<T>::value &&
                  !is_vector<T>::value && !is_byte_array<T>::value;

template <class T>
constexpr WireType wire_type_of() {
    if constexpr (Scalar<T>) return WireType::varint;
    else return WireType::len;
}

// ---- encoding --------------------------------------------------------------------------------

template <Message T>
void encode(Writer& w, const T& msg);

namespace detail {

inline void tag(Writer& w, std::size_t field_no, WireType wt) {
    w.varint((static_cast<u64>(field_no) << 3) | static_cast<u64>(wt));
}

template <Scalar T>
[[nodiscard]] constexpr u64 to_varint(T v) noexcept {
    if constexpr (std::is_enum_v<T>) return static_cast<u64>(static_cast<std::underlying_type_t<T>>(v));
    else if constexpr (SignedScalar<T>) return zigzag(static_cast<i64>(v));
    else return static_cast<u64>(v);
}

template <FixedBytes T>
[[nodiscard]] constexpr const u8* fixed_data(const T& v) noexcept {
    if constexpr (std::same_as<T, Uuid>) return v.bytes.data();
    else return v.data();
}
template <FixedBytes T>
[[nodiscard]] constexpr u8* fixed_data(T& v) noexcept {
    if constexpr (std::same_as<T, Uuid>) return v.bytes.data();
    else return v.data();
}
template <FixedBytes T>
inline constexpr std::size_t fixed_size = sizeof(T);

// Encodes a nested message: 1 length byte is reserved and the body is shifted if it needs more.
template <Message T>
void encode_nested(Writer& w, const T& msg) {
    const std::size_t at = w.size();
    w.put(u8{0});
    encode(w, msg);
    const std::size_t len = w.size() - at - 1;
    const std::size_t vs = varint_size(len);
    if (vs > 1) w.insert_gap(at + 1, vs - 1);
    u8* raw = w.data();
    u64 v = len;
    for (std::size_t i = 0; i < vs; ++i) {
        raw[at + i] = static_cast<u8>((v & 0x7F) | (i + 1 < vs ? 0x80 : 0));
        v >>= 7;
    }
}

// `present` forces emission of default values (explicit presence).
template <class T>
void encode_value(Writer& w, std::size_t no, const T& v, bool present) {
    if constexpr (Scalar<T>) {
        const u64 x = to_varint(v);
        if (x == 0 && !present) return;
        tag(w, no, WireType::varint);
        w.varint(x);
    } else if constexpr (std::same_as<T, std::string> || std::same_as<T, Bytes>) {
        if (v.empty() && !present) return;
        tag(w, no, WireType::len);
        w.varint(v.size());
        w.put(v.data(), v.size());
    } else if constexpr (FixedBytes<T>) {
        tag(w, no, WireType::len);
        w.varint(fixed_size<T>);
        w.put(fixed_data(v), fixed_size<T>);
    } else if constexpr (is_optional<T>::value) {
        if (v) encode_value(w, no, *v, true);
    } else if constexpr (is_vector<T>::value) {
        using E = typename T::value_type;
        if (v.empty()) return;
        if constexpr (Scalar<E>) {
            std::size_t len = 0;
            for (const auto& e : v) len += varint_size(to_varint(e));
            tag(w, no, WireType::len);
            w.varint(len);
            for (const auto& e : v) w.varint(to_varint(e));
        } else {
            for (const auto& e : v) encode_value(w, no, e, true);
        }
    } else {
        static_assert(Message<T>, "unsupported field type");
        tag(w, no, WireType::len);
        encode_nested(w, v);
    }
}

}  // namespace detail

template <Message T>
void encode(Writer& w, const T& msg) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (detail::encode_value(w, I + 1, field<I>(msg), false), ...);
    }(std::make_index_sequence<field_count<T>>{});
}

// ---- decoding --------------------------------------------------------------------------------

template <Message T>
bool decode(std::span<const u8> in, T& msg);

namespace detail {

inline void skip(Reader& r, WireType wt) {
    switch (wt) {
        case WireType::varint: (void)r.varint(); break;
        case WireType::i64: (void)r.bytes(8); break;
        case WireType::len: (void)r.bytes(r.varint()); break;
        case WireType::i32: (void)r.bytes(4); break;
        default: r.fail(); break;
    }
}

template <Scalar T>
void from_varint(Reader& r, u64 x, T& out) {
    if constexpr (std::same_as<T, bool>) {
        out = x != 0;
    } else if constexpr (std::is_enum_v<T>) {
        using U = std::underlying_type_t<T>;
        if (x > static_cast<u64>(std::numeric_limits<U>::max())) return r.fail();
        out = static_cast<T>(static_cast<U>(x));
    } else if constexpr (SignedScalar<T>) {
        const i64 s = unzigzag(x);
        if (s < std::numeric_limits<T>::min() || s > std::numeric_limits<T>::max()) return r.fail();
        out = static_cast<T>(s);
    } else {
        if (x > std::numeric_limits<T>::max()) return r.fail();
        out = static_cast<T>(x);
    }
}

template <class T>
void decode_value(Reader& r, WireType wt, T& out) {
    if constexpr (Scalar<T>) {
        if (wt != WireType::varint) return r.fail();
        from_varint(r, r.varint(), out);
    } else if constexpr (std::same_as<T, std::string>) {
        if (wt != WireType::len) return r.fail();
        auto s = r.bytes(r.varint());
        out.assign(reinterpret_cast<const char*>(s.data()), s.size());
    } else if constexpr (std::same_as<T, Bytes>) {
        if (wt != WireType::len) return r.fail();
        auto s = r.bytes(r.varint());
        out.assign(s.begin(), s.end());
    } else if constexpr (FixedBytes<T>) {
        if (wt != WireType::len) return r.fail();
        auto s = r.bytes(r.varint());
        if (s.size() != fixed_size<T>) return r.fail();
        std::memcpy(fixed_data(out), s.data(), fixed_size<T>);
    } else if constexpr (is_optional<T>::value) {
        if (!out) out.emplace();
        decode_value(r, wt, *out);
    } else if constexpr (is_vector<T>::value) {
        using E = typename T::value_type;
        if constexpr (Scalar<E>) {
            if (wt == WireType::len) {  // packed
                Reader sub(r.bytes(r.varint()));
                while (r.ok() && sub.ok() && !sub.empty()) from_varint(sub, sub.varint(), out.emplace_back());
                if (!sub.ok()) r.fail();
                return;
            }
        }
        decode_value(r, wt, out.emplace_back());
    } else {
        static_assert(Message<T>, "unsupported field type");
        if (wt != WireType::len) return r.fail();
        if (!decode(r.bytes(r.varint()), out)) r.fail();
    }
}

template <Message T, std::size_t... I>
void dispatch(Reader& r, T& msg, u64 field_no, WireType wt, std::index_sequence<I...>) {
    const bool known = ((field_no == I + 1 ? (decode_value(r, wt, field<I>(msg)), true) : false) || ...);
    if (!known) skip(r, wt);
}

}  // namespace detail

// Decodes into `msg` (merging into existing values). Unknown fields are skipped.
template <Message T>
bool decode(std::span<const u8> in, T& msg) {
    Reader r(in);
    while (r.ok() && !r.empty()) {
        const u64 key = r.varint();
        const u64 field_no = key >> 3;
        if (field_no == 0) return false;
        detail::dispatch(r, msg, field_no, static_cast<WireType>(key & 7),
                         std::make_index_sequence<field_count<T>>{});
    }
    return r.ok();
}

template <Message T>
[[nodiscard]] Bytes encode_to_bytes(const T& msg) {
    Writer w;
    encode(w, msg);
    return w.take();
}

}  // namespace sb::wire
