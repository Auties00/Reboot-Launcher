#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/contracts/common.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/testing/fake_random.hpp"

using namespace reboot;

namespace {

std::span<const u8> bytes_of(std::string_view text) {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

std::string hex_sha(std::string_view text) { return to_hex(sha256(bytes_of(text))); }

}  // namespace

TEST_CASE("sha256 matches the FIPS 180-2 vectors", "[foundation][sha256]") {
    CHECK(hex_sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hex_sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hex_sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const std::string million(1'000'000, 'a');
    CHECK(hex_sha(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("Incremental sha256 equals one-shot at every split", "[foundation][sha256]") {
    std::vector<u8> data(300);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<u8>(i * 7 + 3);
    const auto expected = sha256(data);
    for (const std::size_t split : std::array<std::size_t, 11>{0, 1, 55, 56, 63, 64, 65, 127, 128, 200, 300}) {
        Sha256 hash;
        hash.update(std::span<const u8>(data).first(split));
        hash.update(std::span<const u8>(data).subspan(split));
        CHECK(hash.finish() == expected);
    }
    Sha256 bytewise;
    for (const u8 byte : data) bytewise.update({&byte, 1});
    CHECK(bytewise.finish() == expected);
}

TEST_CASE("to_hex is lowercase and constant_time_equal compares by content", "[foundation][sha256]") {
    const std::array<u8, 3> bytes{0x00, 0xAB, 0xFF};
    CHECK(to_hex(bytes) == "00abff");
    CHECK(to_hex({}) == "");
    const std::array<u8, 3> same{0x00, 0xAB, 0xFF};
    const std::array<u8, 3> other{0x00, 0xAB, 0xFE};
    CHECK(constant_time_equal(bytes, same));
    CHECK_FALSE(constant_time_equal(bytes, other));
    CHECK_FALSE(constant_time_equal(bytes, std::span<const u8>(same).first(2)));
    CHECK(constant_time_equal({}, {}));
}

TEST_CASE("uuid_v4 sets the version and variant bits", "[foundation][random]") {
    testing::FakeRandom random(42);
    std::set<std::string> seen;
    for (int i = 0; i < 64; ++i) {
        const Uuid uuid = uuid_v4(random);
        CHECK((uuid.bytes[6] & 0xF0) == 0x40);
        CHECK((uuid.bytes[8] & 0xC0) == 0x80);
        const std::string text = format_uuid(uuid);
        CHECK(text[14] == '4');
        seen.insert(text);
    }
    CHECK(seen.size() == 64);
}

TEST_CASE("random_token_hex has two digits per byte", "[foundation][random]") {
    testing::FakeRandom random(7);
    const std::string token = random_token_hex(random, 32);
    CHECK(token.size() == 64);
    CHECK(token.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(random_token_hex(random, 0).empty());
}

TEST_CASE("OsRandom fills every byte of large and empty buffers", "[foundation][random]") {
    OsRandom random;
    std::vector<u8> big(4096, 0);
    random.fill(big);
    // 4 KiB of CSPRNG output with every byte zero is not a realistic outcome.
    CHECK(std::ranges::any_of(big, [](u8 b) { return b != 0; }));
    std::vector<u8> tail(300, 0);
    random.fill(tail);
    CHECK(std::ranges::any_of(std::span<const u8>(tail).last(44), [](u8 b) { return b != 0; }));
    random.fill({});
    CHECK(random_bytes<16>(random) != random_bytes<16>(random));
}

TEST_CASE("Paths cross the wire without loss", "[foundation][native_path]") {
    NativePath path = NativePath("dir") / "file.txt";
#if defined(_WIN32)
    // An unpaired surrogate survives the native bytes, though not the display text.
    path += std::wstring(1, static_cast<wchar_t>(0xD800));
#else
    path += std::string("\xFF");
#endif
    const WirePath wire = to_wire(path);
    const Result<NativePath> back = from_wire(wire);
    REQUIRE(back);
    CHECK(back->native() == path.native());
    CHECK(wire.display.find("file.txt") != std::string::npos);
    CHECK(wire.display.ends_with("\xEF\xBF\xBD"));
    CHECK(display_utf8(path) == wire.display);
}

TEST_CASE("A malformed wire path is refused", "[foundation][native_path]") {
#if defined(_WIN32)
    const WirePath odd{"x", {0x41}};
    CHECK_FALSE(from_wire(odd));
    const WirePath nul{"x", {0x41, 0x00, 0x00, 0x00}};
#else
    const WirePath nul{"x", {0x41, 0x00}};
#endif
    const Result<NativePath> refused = from_wire(nul);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "foundation.malformed_wire_path");
    CHECK(from_wire(WirePath{})->empty());
}

TEST_CASE("Diagnostics carry typed args and map kinds to exit codes", "[foundation][diag]") {
    const Diagnostic diag = make_diag(ErrorDomain::Play, MessageId{"play.wrong_session"})
                                .arg("count", 3)
                                .arg("big", u64{7})
                                .arg("flag", true)
                                .arg("wait", std::chrono::seconds{2})
                                .arg("name", "x")
                                .arg("kind", ErrorKind::Conflict)
                                .arg("path", NativePath("a"))
                                .kind(ErrorKind::Conflict)
                                .retryable()
                                .cause(internal_bug("inner"))
                                .build();
    CHECK(diag.is(MessageId{"play.wrong_session"}));
    CHECK(std::get<i64>(*diag.find_arg("count")) == 3);
    CHECK(std::get<u64>(*diag.find_arg("big")) == 7);
    CHECK(std::get<bool>(*diag.find_arg("flag")));
    CHECK(std::get<std::chrono::milliseconds>(*diag.find_arg("wait")) == std::chrono::milliseconds{2000});
    CHECK(std::get<std::string>(*diag.find_arg("name")) == "x");
    CHECK(std::get<u64>(*diag.find_arg("kind")) == static_cast<u64>(ErrorKind::Conflict));
    CHECK(std::get<WirePath>(*diag.find_arg("path")).display == "a");
    CHECK(diag.find_arg("missing") == nullptr);
    CHECK(diag.retryable);
    REQUIRE(diag.causes.size() == 1);
    CHECK(diag.causes[0].id == "internal.bug");
    CHECK(std::get<std::string>(*diag.causes[0].find_arg("where")) == "inner");

    CHECK(exit_code_for(diag) == 4);
    Diagnostic copy = diag;
    for (const auto& [kind, code] : {std::pair{ErrorKind::Generic, 1}, std::pair{ErrorKind::InvalidInput, 2},
                                     std::pair{ErrorKind::NotFound, 3}, std::pair{ErrorKind::EngineUnavailable, 5},
                                     std::pair{ErrorKind::Unsupported, 6}, std::pair{ErrorKind::Cancelled, 130}}) {
        copy.kind = kind;
        CHECK(exit_code_for(copy) == code);
    }
}

TEST_CASE("Domain prefixes round-trip through ids", "[foundation][diag]") {
    for (u8 i = 1; i <= static_cast<u8>(ErrorDomain::Platform); ++i) {
        const auto domain = static_cast<ErrorDomain>(i);
        CHECK(domain_from_id(std::string(domain_prefix(domain)) + ".x") == domain);
    }
    CHECK(domain_from_id("nonsense.x") == ErrorDomain::Unknown);
    CHECK(domain_from_id("") == ErrorDomain::Unknown);
    CHECK(domain_from_id("game_channel.closed") == ErrorDomain::GameChannel);
}

TEST_CASE("The message registry is sorted and lists the foundation's ids", "[foundation][diag]") {
    const auto registry = message_registry();
    CHECK(std::ranges::is_sorted(registry, {}, &MessageSpec::id));
    const auto find = [&](std::string_view id) {
        const auto it = std::ranges::find(registry, id, &MessageSpec::id);
        return it == registry.end() ? nullptr : *it;
    };
    const MessageSpec* bug = find("internal.bug");
    REQUIRE(bug != nullptr);
    REQUIRE(bug->args.size() == 1);
    CHECK(bug->args[0].name == "where");
    const MessageSpec* unexpected = find("contracts.unexpected_frame");
    REQUIRE(unexpected != nullptr);
    REQUIRE(unexpected->args.size() == 2);
    CHECK(unexpected->args[0].name == "actual");
    CHECK(unexpected->args[1].name == "expected");
    for (const char* id : {"foundation.invalid_uuid", "foundation.op_not_found", "requests.already_resolved",
                           "requests.not_found", "contracts.malformed_frame"})
        CHECK(find(id) != nullptr);
}

TEST_CASE("Placeholders are counted once each, in order", "[foundation][diag]") {
    STATIC_REQUIRE(detail::count_placeholders("{a} and {b} and {a}") == 2);
    STATIC_REQUIRE(detail::count_placeholders("{} {A} {a-b} {open") == 0);
    constexpr auto specs = detail::placeholders<2>("{first} then {second_2}");
    STATIC_REQUIRE(specs[0].name == "first");
    STATIC_REQUIRE(specs[1].name == "second_2");
}

TEST_CASE("Contract frames encode, split across reads and decode", "[foundation][framing]") {
    const std::vector<u8> ping = encode_contract_frame(contracts::common::Ping{0x1122334455ull});
    const std::vector<u8> pong = encode_contract_frame(contracts::common::Pong{9});
    std::vector<u8> stream = ping;
    stream.insert(stream.end(), pong.begin(), pong.end());

    Framer framer(kChildFrameCap);
    std::vector<u64> types;
    std::optional<u64> nonce;
    const auto on_frame = [&](const RawFrame& frame) {
        types.push_back(frame.type);
        if (is_frame<contracts::common::Ping>(frame)) {
            const auto decoded = decode_contract<contracts::common::Ping>(frame);
            if (!decoded) return false;
            nonce = decoded->nonce;
        }
        return true;
    };
    for (const u8 byte : stream) REQUIRE(framer.feed({&byte, 1}, on_frame) == Framer::Status::ok);
    CHECK(types == std::vector<u64>{0x10, 0x11});
    CHECK(nonce == 0x1122334455ull);
    CHECK(framer.buffered() == 0);

    const RawFrame pong_frame{0x11, std::span<const u8>(pong).subspan(pong.size() - 1)};
    const auto wrong = decode_contract<contracts::common::Ping>(pong_frame);
    REQUIRE_FALSE(wrong);
    CHECK(wrong.error().id == "contracts.unexpected_frame");
}

TEST_CASE("A frame over the cap is too large", "[foundation][framing]") {
    const std::vector<u8> log = encode_contract_frame(contracts::common::Log{LogLevel::Info, 1, std::string(64, 'x')});
    Framer framer(16);
    CHECK(framer.feed(log, [](const RawFrame&) { return true; }) == Framer::Status::too_large);
}

TEST_CASE("The game-control preamble carries the payload ABI", "[foundation][framing]") {
    const auto preamble = game_control_preamble(0x0102);
    CHECK(preamble[0] == 'R');
    CHECK(preamble[5] == 0);
    CHECK(parse_game_control_preamble(preamble) == u16{0x0102});
    auto bad = preamble;
    bad[0] = 'X';
    CHECK_FALSE(parse_game_control_preamble(bad));
}

TEST_CASE("Diagnostics survive the common wire form", "[foundation][contracts]") {
    const Diagnostic diag = make_diag(ErrorDomain::Net, MessageId{"net.timeout"})
                                .arg("host", "example.com")
                                .arg("attempts", -2)
                                .arg("bytes", u64{10})
                                .arg("tls", false)
                                .arg("after", std::chrono::milliseconds{1500})
                                .arg("path", WirePath{"C:/x", {1, 2}})
                                .arg("version", SemVer{1, 2, 3, "rc.1"})
                                .detail("refused")
                                .os(SystemError{SystemError::Origin::GuestWindows, 10061})
                                .retryable()
                                .kind(ErrorKind::EngineUnavailable)
                                .build();
    const Diagnostic back = contracts::common::to_diagnostic(contracts::common::to_wire(diag));
    CHECK(back.domain == ErrorDomain::Net);
    CHECK(back.id == "net.timeout");
    CHECK(back.kind == ErrorKind::EngineUnavailable);
    CHECK(back.retryable);
    CHECK(back.detail == "refused");
    CHECK(back.os_error == SystemError{SystemError::Origin::GuestWindows, 10061});
    CHECK(std::get<std::string>(*back.find_arg("host")) == "example.com");
    CHECK(std::get<i64>(*back.find_arg("attempts")) == -2);
    CHECK(std::get<u64>(*back.find_arg("bytes")) == 10);
    CHECK_FALSE(std::get<bool>(*back.find_arg("tls")));
    CHECK(std::get<std::chrono::milliseconds>(*back.find_arg("after")) == std::chrono::milliseconds{1500});
    CHECK(std::get<WirePath>(*back.find_arg("path")).display == "C:/x");
    CHECK(std::get<std::string>(*back.find_arg("version")) == "1.2.3-rc.1");

    // A value that does not parse as its kind stays text.
    contracts::common::WireDiagnostic odd = contracts::common::to_wire(diag);
    odd.args[1].value = "not-a-number";
    CHECK(std::get<std::string>(*contracts::common::to_diagnostic(odd).find_arg("attempts")) == "not-a-number");
    CHECK(contracts::common::to_diagnostic(contracts::common::WireDiagnostic{.id = "who.knows"}).domain ==
          ErrorDomain::Unknown);
}
