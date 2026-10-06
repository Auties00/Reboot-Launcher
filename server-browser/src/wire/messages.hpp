#pragma once

// Wire schema for ALPN "rbsb/1". proto/rbsb.proto mirrors these structs field for field;
// field numbers are declaration order + 1, so fields may only ever be appended.

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/types.hpp"
#include "wire/codec.hpp"

namespace sb::wire {

inline constexpr std::string_view kAlpn = "rbsb/1";
inline constexpr u32 kProtoMinor = 0;

namespace feature {
inline constexpr u64 datagrams = 1u << 0;  // deltas and heartbeats over QUIC DATAGRAM frames
inline constexpr u64 zstd = 1u << 1;       // snapshots may arrive as SnapshotZstd
}  // namespace feature

namespace entry_flag {
inline constexpr u32 has_password = 1u << 0;
inline constexpr u32 reachable = 1u << 1;
inline constexpr u32 online = 1u << 2;
inline constexpr u32 hidden = 1u << 3;
}  // namespace entry_flag

// Version buckets: 0 selects every bucket in a ViewSpec, 1 holds unparseable versions,
// otherwise 2 + major * 1024 + minor.
inline constexpr u32 kBucketAll = 0;
inline constexpr u32 kBucketOther = 1;

// Region codes (continents); 0 means unknown on an entry and "all" in a ViewSpec.
enum class Region : u32 { all = 0, africa = 1, antarctica = 2, asia = 3, europe = 4, north_america = 5, oceania = 6, south_america = 7 };
inline constexpr u32 kRegionCount = 8;

enum class Role : u32 { unknown = 0, browser = 1, host = 2 };
enum class Sort : u32 { players = 0, newest = 1, name = 2 };
enum class PasswordFilter : u32 { any = 0, none = 1, only = 2 };
enum class ErrorCode : u32 {
    unknown = 0,
    bad_request = 1,
    unsupported = 2,
    rate_limited = 3,
    not_found = 4,
    wrong_password = 5,
    unauthorized = 6,
    unreachable = 7,
    limit_exceeded = 8,
    conflict = 9,
    internal = 10,
    unavailable = 11,
};
enum class GoAwayReason : u32 { shutdown = 0, overload = 1, rebalance = 2 };

// ---- session ---------------------------------------------------------------------------------

struct Hello {
    Role role{};
    u32 proto_minor = 0;
    std::string client_version;
    u64 features = 0;
};

struct Limits {
    u32 heartbeat_ms = 0;
    u32 ttl_ms = 0;
    u32 max_subscriptions = 0;
    u32 max_window = 0;
    u32 max_query_limit = 0;
    u32 host_update_burst = 0;
    u32 host_update_per_sec = 0;
};

struct Welcome {
    u64 edge_id = 0;
    u32 proto_minor = 0;
    u64 features = 0;
    u64 server_time_ms = 0;
    Limits limits;
};

struct Ack {
    u32 req_id = 0;
};

struct Error {
    u32 req_id = 0;
    ErrorCode code{};
    std::string message;
    u32 retry_after_ms = 0;
};

struct GoAway {
    GoAwayReason reason{};
    u32 reconnect_after_ms = 0;
};

// ---- browsing --------------------------------------------------------------------------------

struct ViewSpec {
    u32 bucket = kBucketAll;
    PasswordFilter password{};
    Region region{};
    Sort sort{};

    constexpr bool operator==(const ViewSpec&) const = default;
};

struct ListEntry {
    u64 handle = 0;
    Uuid id;
    std::string name;
    std::string author;
    std::string version;
    u32 bucket = 0;
    u32 players = 0;
    u32 max_players = 0;
    u32 flags = 0;
    Region region{};
    u64 created_ms = 0;
};

struct Subscribe {
    u32 req_id = 0;
    u32 sub_id = 0;
    ViewSpec view;
    u32 window = 0;
};

struct SubOpen {
    u32 req_id = 0;
    u32 sub_id = 0;
    u32 view_id = 0;
    u32 window = 0;
};

struct Unsubscribe {
    u32 sub_id = 0;
};

// One change to one entry of one view. Fields present in the patch replace the client's copy
// when `vseq` is newer than the per-field sequence the client holds for that entry.
struct Patch {
    u64 handle = 0;
    u64 vseq = 0;
    bool removed = false;
    std::optional<ListEntry> entry;  // full replacement (insert or repair)
    std::optional<u32> players;
    std::optional<u32> max_players;
    std::optional<u32> flags;
    std::optional<std::string> name;
};

// The complete membership of a view's window at `vseq`. Entries the client holds that are not
// listed (and not newer than vseq) left the window. Sent instead of per-entry removals when a
// connection fell behind, so catching up costs a few bytes per member rather than a snapshot.
struct WindowSync {
    u64 vseq = 0;
    std::vector<u64> handles;
};

struct Delta {
    u32 view_id = 0;
    std::vector<Patch> patches;
    std::optional<WindowSync> sync;  // applied before the patches of the same frame
};

struct Snapshot {
    u32 view_id = 0;
    u64 vseq = 0;
    u32 total = 0;
    std::vector<ListEntry> entries;
};

struct Query {
    u32 req_id = 0;
    ViewSpec view;
    std::string text;
    u32 limit = 0;
    Bytes cursor;
};

struct QueryResult {
    u32 req_id = 0;
    std::vector<ListEntry> entries;
    Bytes next_cursor;
    u32 total = 0;
};

struct Resolve {
    u32 req_id = 0;
    Uuid id;
};

struct EntryDetails {
    ListEntry entry;
    std::string description;
    u64 updated_ms = 0;
};

struct ResolveResult {
    u32 req_id = 0;
    std::optional<EntryDetails> details;
};

struct Join {
    u32 req_id = 0;
    Uuid id;
    std::optional<std::string> password;
};

struct JoinGrant {
    u32 req_id = 0;
    Bytes address;  // 4 bytes (IPv4) or 16 bytes (IPv6), network order
    u32 port = 0;
    Bytes ticket;
    u64 expires_ms = 0;
};

// ---- hosting ---------------------------------------------------------------------------------

using Token = std::array<u8, 32>;

struct HostRegister {
    u32 req_id = 0;
    Uuid id;
    std::optional<Token> token;  // absent on the very first registration of `id`
    std::string name;
    std::string description;
    std::string version;
    std::string author;
    u32 game_port = 0;
    std::optional<std::string> password;
    u32 max_players = 0;
    bool hidden = false;
    u32 players = 0;
};

struct HostRegistered {
    u32 req_id = 0;
    std::optional<Token> token;  // only on the first registration of an id
    u64 handle = 0;
    u32 heartbeat_ms = 0;
    u32 ttl_ms = 0;
    Bytes observed_address;
};

// Partial update; req_id 0 asks for no Ack (used for high-frequency player counts).
struct HostUpdate {
    u32 req_id = 0;
    std::optional<std::string> name;
    std::optional<std::string> description;
    std::optional<std::string> version;
    std::optional<std::string> author;
    std::optional<u32> game_port;
    std::optional<std::string> password;  // empty string removes the password
    std::optional<u32> max_players;
    std::optional<bool> hidden;
    std::optional<u32> players;
};

struct HostUnregister {
    u32 req_id = 0;
};

struct HostHeartbeat {
    u32 seq = 0;
};

struct HostStatus {
    bool reachable = false;
    u32 probe_failures = 0;
};

// ---- framing ---------------------------------------------------------------------------------

enum class FrameType : u64 {
    hello = 1,
    subscribe = 2,
    unsubscribe = 3,
    query = 4,
    resolve = 5,
    join = 6,
    host_register = 7,
    host_update = 8,
    host_unregister = 9,

    welcome = 32,
    sub_open = 33,
    query_result = 34,
    resolve_result = 35,
    join_grant = 36,
    host_registered = 37,
    ack = 38,
    error = 39,
    go_away = 40,
    host_status = 41,

    snapshot = 48,
    snapshot_zstd = 49,  // payload: varint raw_len, then zstd(Snapshot)

    delta = 64,
    host_heartbeat = 65,
};

template <class T>
struct FrameTypeOf;
#define SB_FRAME(T, ft) \
    template <>         \
    struct FrameTypeOf<T> { static constexpr FrameType value = FrameType::ft; }
SB_FRAME(Hello, hello);
SB_FRAME(Subscribe, subscribe);
SB_FRAME(Unsubscribe, unsubscribe);
SB_FRAME(Query, query);
SB_FRAME(Resolve, resolve);
SB_FRAME(Join, join);
SB_FRAME(HostRegister, host_register);
SB_FRAME(HostUpdate, host_update);
SB_FRAME(HostUnregister, host_unregister);
SB_FRAME(Welcome, welcome);
SB_FRAME(SubOpen, sub_open);
SB_FRAME(QueryResult, query_result);
SB_FRAME(ResolveResult, resolve_result);
SB_FRAME(JoinGrant, join_grant);
SB_FRAME(HostRegistered, host_registered);
SB_FRAME(Ack, ack);
SB_FRAME(Error, error);
SB_FRAME(GoAway, go_away);
SB_FRAME(HostStatus, host_status);
SB_FRAME(Snapshot, snapshot);
SB_FRAME(Delta, delta);
SB_FRAME(HostHeartbeat, host_heartbeat);
#undef SB_FRAME

template <class T>
inline constexpr FrameType frame_type_v = FrameTypeOf<T>::value;

}  // namespace sb::wire
