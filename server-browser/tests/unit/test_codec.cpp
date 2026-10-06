#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>

#include "wire/frame.hpp"
#include "wire/messages.hpp"

using namespace sb;
using namespace sb::wire;

namespace {

Bytes load_vector(const char* name) {
    std::ifstream f(std::string(SB_VECTORS_DIR) + "/" + name + ".bin", std::ios::binary);
    REQUIRE(f.good());
    return {std::istreambuf_iterator<char>(f), {}};
}

Uuid uuid_from(std::initializer_list<int> bytes) {
    Uuid u;
    std::size_t i = 0;
    for (int b : bytes) u.bytes[i++] = static_cast<u8>(b);
    return u;
}

Uuid uuid_fill(u8 v) {
    Uuid u;
    u.bytes.fill(v);
    return u;
}

// Encoding must match protoc byte for byte, and decoding protoc output must round-trip.
template <class T>
void check_golden(const char* name, const T& msg) {
    const Bytes expected = load_vector(name);
    INFO("vector " << name);
    CHECK(encode_to_bytes(msg) == expected);
    T decoded{};
    REQUIRE(decode(expected, decoded));
    CHECK(encode_to_bytes(decoded) == expected);
}

}  // namespace

TEST_CASE("codec matches protoc golden vectors", "[codec]") {
    check_golden("hello", Hello{.role = Role::browser, .client_version = "reboot-launcher/11.0.0", .features = 3});

    check_golden("welcome", Welcome{.edge_id = 1311768467463790320ULL,
                                    .features = 3,
                                    .server_time_ms = 1759651200000ULL,
                                    .limits = {10000, 30000, 8, 200, 100, 8, 4}});

    check_golden("subscribe", Subscribe{.req_id = 7,
                                        .sub_id = 1,
                                        .view = {4101, PasswordFilter::none, Region::europe, Sort::players},
                                        .window = 200});

    Delta delta{.view_id = 300};
    delta.patches.push_back({.handle = 17, .vseq = 99999, .players = 0u});
    delta.patches.push_back({.handle = 18, .vseq = 100000, .removed = true});
    delta.patches.push_back(
        {.handle = 19, .vseq = 100001, .max_players = 100u, .flags = 3u, .name = std::string("Late Game \xF0\x9F\x8E\xAE")});
    delta.patches.push_back({.handle = 20,
                             .vseq = 100002,
                             .entry = ListEntry{.handle = 20,
                                                .id = uuid_from({0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01,
                                                                 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}),
                                                .name = "Arena",
                                                .author = "auties",
                                                .version = "4.5",
                                                .bucket = 4101,
                                                .players = 12,
                                                .max_players = 100,
                                                .flags = 7,
                                                .region = Region::europe,
                                                .created_ms = 1759651200123ULL}});
    check_golden("delta", delta);

    check_golden("delta_sync", Delta{.view_id = 7,
                                     .patches = {{.handle = 3, .vseq = 500, .players = 9u}},
                                     .sync = WindowSync{.vseq = 500, .handles = {3, 17, 300, 70000}}});

    Snapshot snap{.view_id = 2, .vseq = ~u64{0}, .total = 2};
    snap.entries.push_back({.handle = 1,
                            .id = uuid_from({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}),
                            .name = "A",
                            .author = "x",
                            .version = "1.7.2",
                            .bucket = 1033,
                            .players = 1,
                            .max_players = 16,
                            .flags = 6,
                            .region = Region::north_america,
                            .created_ms = 1});
    snap.entries.push_back(
        {.handle = 2,
         .id = uuid_fill(0xff),
         .name = "A very long server name that pushes the nested entry past one hundred twenty seven bytes!!",
         .author = "someone-with-a-long-name",
         .version = "34.10",
         .bucket = 34828});
    check_golden("snapshot", snap);

    Token token;
    for (std::size_t i = 0; i < token.size(); ++i) token[i] = static_cast<u8>(i);
    check_golden("host_register", HostRegister{.req_id = 1,
                                               .id = uuid_from({0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x10,
                                                                0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe}),
                                               .token = token,
                                               .name = "Auties' Server",
                                               .version = "5.00",
                                               .author = "auties",
                                               .game_port = 7777,
                                               .password = std::string(),
                                               .max_players = 100});

    check_golden("host_update",
                 HostUpdate{.password = std::string("hunter2"), .hidden = false, .players = 0u});

    check_golden("error", Error{.req_id = 4294967295u,
                                .code = ErrorCode::rate_limited,
                                .message = "slow down",
                                .retry_after_ms = 1500});

    check_golden("join_grant", JoinGrant{.req_id = 3,
                                         .address = {0xc0, 0x63, 0xd8, 0x2a},
                                         .port = 7777,
                                         .ticket = {0xde, 0xad, 0xbe, 0xef},
                                         .expires_ms = 1759651260000ULL});

    check_golden("resolve_result",
                 ResolveResult{.req_id = 9,
                               .details = EntryDetails{.entry = {.handle = 5,
                                                                 .id = uuid_from({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                                                  0, 0, 0, 5}),
                                                                 .name = "Hidden",
                                                                 .flags = 12},
                                                       .description = "Private lobby",
                                                       .updated_ms = 42}});

    check_golden("empty_resolve_result", ResolveResult{.req_id = 10});
}

namespace {
struct Packed {
    std::vector<u32> values;
    std::vector<i64> signed_values;
    std::vector<std::string> names;
};
}  // namespace

TEST_CASE("packed and repeated fields round-trip", "[codec]") {
    Packed p{{0, 1, 300, 0xFFFFFFFFu}, {-1, 0, 1, std::numeric_limits<i64>::min()}, {"", "a", "bc"}};
    const Bytes b = encode_to_bytes(p);
    Packed q;
    REQUIRE(decode(b, q));
    CHECK(q.values == p.values);
    CHECK(q.signed_values == p.signed_values);
    CHECK(q.names == p.names);
}

TEST_CASE("decoder rejects malformed input and skips unknown fields", "[codec]") {
    Hello h;
    // Truncated length-delimited field.
    const Bytes truncated = {0x1a, 0x05, 'a', 'b'};
    CHECK_FALSE(decode(truncated, h));
    // Wrong wire type for a known field.
    Hello h2;
    const Bytes wrong_type = {0x0a, 0x00};
    CHECK_FALSE(decode(wrong_type, h2));
    // Value too large for a u32 field.
    Ack a;
    const Bytes too_big = {0x08, 0x80, 0x80, 0x80, 0x80, 0x10};
    CHECK_FALSE(decode(too_big, a));
    // Unknown fields of every wire type are skipped.
    Ack b;
    const Bytes unknown = {0x78, 0x05, 0x82, 0x01, 0x02, 'h', 'i', 0x7d, 1, 2, 3, 4, 0x79, 1, 2, 3, 4, 5, 6, 7, 8, 0x08, 0x2a};
    REQUIRE(decode(unknown, b));
    CHECK(b.req_id == 42);
    // Field number zero and group wire types are invalid.
    Ack c;
    CHECK_FALSE(decode(Bytes{0x00, 0x01}, c));
    CHECK_FALSE(decode(Bytes{0x0b}, c));
}

TEST_CASE("frames split across stream reads", "[codec]") {
    Writer w;
    encode_frame(w, Hello{.role = Role::host, .client_version = "x"});
    encode_frame(w, Ack{.req_id = 5});
    encode_frame(w, Error{.req_id = 6, .message = std::string(300, 'e')});
    const Bytes all = w.take();

    for (std::size_t chunk = 1; chunk <= all.size(); ++chunk) {
        StreamFramer framer(16 * 1024);
        std::vector<FrameType> seen;
        for (std::size_t off = 0; off < all.size(); off += chunk) {
            const std::size_t n = std::min(chunk, all.size() - off);
            auto st = framer.feed(std::span<const u8>(all.data() + off, n), [&](const FrameView& f) {
                seen.push_back(f.type);
                if (f.type == FrameType::error) {
                    Error e;
                    REQUIRE(decode_frame(f, e));
                    CHECK(e.message.size() == 300);
                }
                return true;
            });
            REQUIRE(st == StreamFramer::Status::ok);
        }
        CHECK(seen == std::vector<FrameType>{FrameType::hello, FrameType::ack, FrameType::error});
        CHECK(framer.buffered() == 0);
    }

    StreamFramer small(16);
    CHECK(small.feed(all, [](const FrameView&) { return true; }) == StreamFramer::Status::too_large);
}

TEST_CASE("datagram frames use two-byte lengths", "[codec]") {
    Writer w;
    encode_frame(w, Delta{.view_id = 1, .patches = {{.handle = 1, .vseq = 2, .players = 3u}}}, LenWidth::two);
    const auto v = w.view();
    REQUIRE(v.size() > 3);
    CHECK(v[0] == 0x40);  // type 64 needs a 2-byte varint: 0x40 0x40
    CHECK(v[1] == 0x40);
    CHECK((v[2] & 0xC0) == 0x40);  // 2-byte length
    std::size_t frames = 0;
    CHECK(for_each_frame(v, 1200, [&](const FrameView& f) {
        Delta d;
        REQUIRE(decode_frame(f, d));
        CHECK(d.patches.at(0).players == 3u);
        ++frames;
        return true;
    }));
    CHECK(frames == 1);
}

TEST_CASE("writer grows, reuses recycled buffers and hands them back", "[codec]") {
    Writer w(4);
    for (u32 i = 0; i < 1000; ++i) w.varint(i * 977u);
    w.put(std::string_view("tail"));
    Reader r(w.view());
    for (u32 i = 0; i < 1000; ++i) CHECK(r.varint() == i * 977u);
    CHECK(r.remaining() == 4);

    Bytes taken = w.take();
    CHECK(w.size() == 0);
    const std::size_t cap = taken.capacity();
    const u8* storage = taken.data();
    Writer again(std::move(taken), 16);
    again.put(u8{7});
    CHECK(again.size() == 1);
    CHECK(again.data() == storage);  // recycled allocation, not a new one
    CHECK(again.view()[0] == 7);
    const Bytes out = again.take();
    CHECK(out.size() == 1);
    CHECK(out.capacity() == cap);

    Writer g;
    g.put(std::string_view("abef"));
    g.insert_gap(2, 2);
    g.data()[2] = 'c';
    g.data()[3] = 'd';
    CHECK(std::string_view(reinterpret_cast<const char*>(g.data()), g.size()) == "abcdef");
}
