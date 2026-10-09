#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/ipc/ipc_codec.hpp"

using namespace rb;
using rb::ipc::ClientMessage;
using rb::ipc::EngineMessage;
using rb::ipc::IpcCodec;
namespace wire = rb::contracts::ipc;

namespace {

[[nodiscard]] wire::Bytes bytes_of(std::string_view text) { return {text.begin(), text.end()}; }

[[nodiscard]] std::vector<u8> concat(std::initializer_list<std::vector<u8>> parts) {
    std::vector<u8> out;
    for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
    return out;
}

}  // namespace

TEST_CASE("every client message survives encode and decode", "[ipc][codec]") {
    const std::vector<ClientMessage> messages{
        wire::Hello{wire::ClientKind::Cli, "1.0.0", 0x10000, 42, {"7", true, {{"DISPLAY", ":0"}}}},
        wire::Call{1, wire::kEngineStatus, bytes_of("req"), 500},
        wire::Start{2, wire::method_id(3, 1), bytes_of("go"), false},
        wire::Cancel{3},
        wire::Attach{4},
        wire::Release{5},
        wire::Subscribe{6, bytes_of("filter")},
        wire::Unsubscribe{6},
        wire::Credit{6, 17},
        wire::SecretPut{bytes_of("t"), bytes_of("s")},
        wire::SecretReveal{7, bytes_of("t")},
        wire::LogWrite{LogLevel::Warn, "hello"},
        contracts::common::Ping{9},
        wire::Goodbye{wire::GoodbyeReason::Normal},
    };
    std::vector<u8> stream;
    for (const ClientMessage& message : messages) {
        const auto frame = IpcCodec::encode(message);
        stream.insert(stream.end(), frame.begin(), frame.end());
    }
    IpcCodec codec;
    auto decoded = codec.feed_from_client(stream);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == messages.size());
    for (std::size_t i = 0; i < messages.size(); ++i) {
        CHECK(decoded->at(i).index() == messages[i].index());
        CHECK(IpcCodec::encode(decoded->at(i)) == IpcCodec::encode(messages[i]));
    }
    CHECK(codec.buffered() == 0);
}

TEST_CASE("every engine message survives encode and decode", "[ipc][codec]") {
    const std::vector<EngineMessage> messages{
        wire::HelloAck{"1.0.0", 3, 77, WirePath{"/x", {}}, WirePath{"/r", {}}, wire::EngineOrigin::Foreground,
                       wire::StorageMode::ReadOnly, true, wire::Compatibility::BootstrapOnly},
        wire::Reply{1, bytes_of("ok"), std::nullopt},
        wire::Started{2, 3},
        wire::OpResult{3, bytes_of("outcome")},
        wire::EventBatch{4, {wire::WireEvent{1, 2, 3, std::nullopt, 9, bytes_of("p")}}},
        wire::Resync{4},
        wire::ForegroundHint{1234},
        contracts::common::Pong{9},
        wire::Goodbye{wire::GoodbyeReason::SlowConsumer},
    };
    IpcCodec codec;
    std::vector<EngineMessage> decoded;
    for (const EngineMessage& message : messages) {
        auto batch = codec.feed_from_engine(IpcCodec::encode(message));
        REQUIRE(batch.has_value());
        decoded.insert(decoded.end(), batch->begin(), batch->end());
    }
    REQUIRE(decoded.size() == messages.size());
    for (std::size_t i = 0; i < messages.size(); ++i)
        CHECK(IpcCodec::encode(decoded[i]) == IpcCodec::encode(messages[i]));
}

TEST_CASE("frames split at every byte are reassembled", "[ipc][codec]") {
    const auto first = IpcCodec::encode(wire::Call{1, 2, bytes_of("payload"), 0});
    const auto second = IpcCodec::encode(wire::Credit{1, 2});
    const auto stream = concat({first, second});
    IpcCodec codec;
    std::vector<ClientMessage> decoded;
    for (std::size_t i = 0; i < stream.size(); ++i) {
        auto part = codec.feed_from_client(std::span<const u8>(stream).subspan(i, 1));
        REQUIRE(part.has_value());
        decoded.insert(decoded.end(), part->begin(), part->end());
        if (i + 1 < first.size()) CHECK(codec.buffered() == i + 1);
    }
    REQUIRE(decoded.size() == 2);
    CHECK(std::get<wire::Call>(decoded[0]).payload == bytes_of("payload"));
    CHECK(std::get<wire::Credit>(decoded[1]).n == 2);
    CHECK(codec.buffered() == 0);
}

TEST_CASE("a frame and a half keeps only the half", "[ipc][codec]") {
    const auto first = IpcCodec::encode(wire::Cancel{1});
    const auto second = IpcCodec::encode(wire::Cancel{2});
    IpcCodec codec;
    auto part = codec.feed_from_client(concat({first, std::vector<u8>(second.begin(), second.begin() + 2)}));
    REQUIRE(part.has_value());
    CHECK(part->size() == 1);
    CHECK(codec.buffered() == 2);
    auto rest = codec.feed_from_client(std::span<const u8>(second).subspan(2));
    REQUIRE(rest.has_value());
    REQUIRE(rest->size() == 1);
    CHECK(std::get<wire::Cancel>(rest->front()).op_id == 2);
    CHECK(codec.buffered() == 0);
}

TEST_CASE("a length over kIpcFrameCap is malformed before its body arrives", "[ipc][codec]") {
    // Type 0x101, then an 8-byte varint length of kIpcFrameCap + 1.
    const u64 length = kIpcFrameCap + 1;
    const std::vector<u8> header{0x41, 0x01, 0xC0, 0, 0, 0, static_cast<u8>(length >> 24), static_cast<u8>(length >> 16),
                                 static_cast<u8>(length >> 8), static_cast<u8>(length)};
    IpcCodec codec;
    auto result = codec.feed_from_client(header);
    REQUIRE_FALSE(result);
    CHECK(result.error().is(kMalformedFrame));
    CHECK(codec.buffered() == 0);
}

TEST_CASE("a frame of the other direction or an unknown type is unexpected", "[ipc][codec]") {
    IpcCodec client_side;
    auto wrong = client_side.feed_from_client(IpcCodec::encode(wire::Resync{1}));
    REQUIRE_FALSE(wrong);
    CHECK(wrong.error().is(kUnexpectedFrame));

    IpcCodec engine_side;
    auto unknown = engine_side.feed_from_engine(std::vector<u8>{0x3F, 0x00});
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().is(kUnexpectedFrame));
}

TEST_CASE("a payload that does not decode is malformed", "[ipc][codec]") {
    // A Call whose first field claims more bytes than the frame holds.
    const std::vector<u8> frame{0x41, 0x01, 0x02, 0x0A, 0x05};
    IpcCodec codec;
    auto result = codec.feed_from_client(frame);
    REQUIRE_FALSE(result);
    CHECK(result.error().is(kMalformedFrame));
}

TEST_CASE("decode_client and decode_engine pick the message by frame type", "[ipc][codec]") {
    CHECK_FALSE(IpcCodec::decode_client(RawFrame{contract_frame_type_v<wire::Started>, {}}));
    auto started = IpcCodec::decode_engine(RawFrame{contract_frame_type_v<wire::Started>, {}});
    REQUIRE(started.has_value());
    CHECK(std::holds_alternative<wire::Started>(*started));
    auto goodbye = IpcCodec::decode_client(RawFrame{contract_frame_type_v<wire::Goodbye>, {}});
    REQUIRE(goodbye.has_value());
    CHECK(std::holds_alternative<wire::Goodbye>(*goodbye));
}

TEST_CASE("secret frames match the generic encoding and are sized exactly", "[ipc][codec]") {
    const std::vector<std::pair<wire::Bytes, wire::Bytes>> cases{
        {bytes_of("account"), bytes_of("hunter2")},
        {{}, bytes_of("x")},
        {bytes_of("t"), {}},
        {bytes_of("big"), wire::Bytes(300, 0x41)},
    };
    for (const auto& [target, secret] : cases) {
        const SecretBytes frame = IpcCodec::encode_secret_put(target, SecretBytes{secret});
        CHECK(frame.reveal() == IpcCodec::encode(wire::SecretPut{target, secret}));
        CHECK(frame.reveal().capacity() == frame.reveal().size());
    }
    for (const u64 req_id : {u64{0}, u64{1}, u64{300}, u64{1} << 40}) {
        const SecretBytes reply = IpcCodec::encode_secret_reply(req_id, SecretBytes{bytes_of("pw")});
        CHECK(reply.reveal() == IpcCodec::encode(wire::Reply{req_id, bytes_of("pw"), std::nullopt}));
        CHECK(reply.reveal().capacity() == reply.reveal().size());
    }
    const SecretBytes empty = IpcCodec::encode_secret_reply(3, SecretBytes{});
    CHECK(empty.reveal() == IpcCodec::encode(wire::Reply{3, wire::Bytes{}, std::nullopt}));
}

TEST_CASE("a decoded SecretPut carries the secret bytes", "[ipc][codec]") {
    const SecretBytes frame = IpcCodec::encode_secret_put(bytes_of("target"), SecretBytes{bytes_of("hunter2")});
    IpcCodec codec;
    auto decoded = codec.feed_from_client(frame.reveal());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);
    const auto& put = std::get<wire::SecretPut>(decoded->front());
    CHECK(put.target == bytes_of("target"));
    CHECK(put.bytes == bytes_of("hunter2"));
}

TEST_CASE("fits_frame_cap follows the cap feed_from_client enforces", "[ipc][codec]") {
    const wire::Bytes at_cap = IpcCodec::encode(wire::LogWrite{LogLevel::Info, std::string(kIpcFrameCap - 8, 'x')});
    const wire::Bytes over_cap = IpcCodec::encode(wire::LogWrite{LogLevel::Info, std::string(kIpcFrameCap, 'x')});
    CHECK(IpcCodec::fits_frame_cap(at_cap));
    CHECK_FALSE(IpcCodec::fits_frame_cap(over_cap));
    CHECK_FALSE(IpcCodec::fits_frame_cap(wire::Bytes{0x41}));

    IpcCodec codec;
    CHECK(codec.feed_from_client(at_cap).has_value());
    CHECK_FALSE(codec.feed_from_client(over_cap).has_value());
}
