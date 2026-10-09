#pragma once

#include <compare>
#include <cstddef>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>

#include "core/types.hpp"
#include "reboot/foundation/result_fwd.hpp"

namespace rb {

using sb::i32;
using sb::i64;
using sb::u16;
using sb::u32;
using sb::u64;
using sb::u8;

using Uuid = sb::Uuid;

[[nodiscard]] inline std::string format_uuid(const Uuid& uuid) { return uuid.to_string(); }

// Case-insensitive; accepts the canonical 8-4-4-4-12 form only.
[[nodiscard]] Result<Uuid> parse_uuid(std::string_view text);

[[nodiscard]] inline std::size_t hash_uuid(const Uuid& uuid) noexcept {
    u64 lo = 0;
    u64 hi = 0;
    std::memcpy(&lo, uuid.bytes.data(), sizeof lo);
    std::memcpy(&hi, uuid.bytes.data() + sizeof lo, sizeof hi);
    return static_cast<std::size_t>(lo ^ (hi * 0x9E3779B97F4A7C15ull));
}

template <class Tag>
struct Id {
    Uuid value;

    constexpr auto operator<=>(const Id&) const = default;
};

using SessionId = Id<struct SessionTag>;
using BuildId = Id<struct BuildTag>;
using HostProfileId = Id<struct HostProfileTag>;
using AccountRecordId = Id<struct AccountRecordTag>;
using ServerId = Id<struct ServerTag>;

template <class Tag, class Rep>
struct Counter {
    Rep value{};

    constexpr auto operator<=>(const Counter&) const = default;
};

using OpId = Counter<struct OpTag, u64>;
using SubscriptionId = Counter<struct SubscriptionTag, u64>;
using RequestId = Counter<struct RequestTag, u64>;
using EngineEpoch = Counter<struct EngineEpochTag, u64>;

using CatalogEntryId = std::string;

}  // namespace rb

template <class Tag>
struct std::hash<rb::Id<Tag>> {
    std::size_t operator()(const rb::Id<Tag>& id) const noexcept { return rb::hash_uuid(id.value); }
};

template <class Tag, class Rep>
struct std::hash<rb::Counter<Tag, Rep>> {
    std::size_t operator()(const rb::Counter<Tag, Rep>& c) const noexcept { return std::hash<Rep>{}(c.value); }
};
