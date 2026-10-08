#include <catch2/catch_test_macros.hpp>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "reboot/contracts/backend.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/process/child_channel.hpp"

using namespace reboot;
using namespace reboot::process;

namespace be = reboot::contracts::backend;
namespace common = reboot::contracts::common;

namespace {

struct Frame {
    u64 type = 0;
    std::vector<u8> payload;
};

struct Harness {
    ChildHandshake handshake = make_child_handshake<be::BackendHello>(
        be::kBackendProtocol, [](const be::BackendHello& hello) { return hello.protocol; },
        [](const be::BackendHello&) -> Result<be::BackendWelcome> { return be::BackendWelcome{"127.0.0.1", {}, {}}; });
    std::vector<u8> written;
    std::vector<Frame> received;
    ChildChannel channel{"reboot-backend", handshake,
                         [this](std::span<const u8> bytes) { written.insert(written.end(), bytes.begin(), bytes.end()); },
                         [this](const RawFrame& frame) {
                             received.push_back({frame.type, std::vector<u8>(frame.payload.begin(), frame.payload.end())});
                         }};

    Result<void> feed(const std::vector<u8>& bytes) { return channel.feed(bytes); }

    // Every frame the engine wrote, in order.
    std::vector<Frame> sent() {
        std::vector<Frame> out;
        Framer framer(kChildFrameCap);
        (void)framer.feed(written, [&](const RawFrame& frame) {
            out.push_back({frame.type, std::vector<u8>(frame.payload.begin(), frame.payload.end())});
            return true;
        });
        return out;
    }
};

std::vector<u8> hello(u32 protocol) { return encode_contract_frame(be::BackendHello{protocol, "1.0", {}}); }

}  // namespace

TEST_CASE("a matching Hello opens the channel and gets the Welcome", "[process][channel]") {
    Harness h;
    REQUIRE(h.feed(hello(be::kBackendProtocol)).has_value());
    CHECK(h.channel.phase() == ChannelPhase::Open);
    const std::vector<Frame> sent = h.sent();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].type == contract_frame_type_v<be::BackendWelcome>);
}

TEST_CASE("another protocol closes the channel with child_protocol_mismatch", "[process][channel]") {
    Harness h;
    Result<void> fed = h.feed(hello(be::kBackendProtocol + 1));
    REQUIRE_FALSE(fed.has_value());
    CHECK(fed.error().id == "process.child_protocol_mismatch");
    CHECK(h.channel.phase() == ChannelPhase::Closed);
    CHECK(h.written.empty());
}

TEST_CASE("a frame before Hello closes the channel", "[process][channel]") {
    Harness h;
    Result<void> fed = h.feed(encode_contract_frame(be::Ready{3551}));
    REQUIRE_FALSE(fed.has_value());
    CHECK(fed.error().id == "process.child_hello_expected");
    CHECK(h.channel.phase() == ChannelPhase::Closed);
}

TEST_CASE("frames after Welcome reach the sink and Ping is answered", "[process][channel]") {
    Harness h;
    std::vector<u8> bytes = hello(be::kBackendProtocol);
    const std::vector<u8> ping = encode_contract_frame(common::Ping{7});
    const std::vector<u8> ready = encode_contract_frame(be::Ready{3551});
    bytes.insert(bytes.end(), ping.begin(), ping.end());
    bytes.insert(bytes.end(), ready.begin(), ready.end());

    // One byte at a time, so every frame boundary is split.
    for (const u8 byte : bytes) REQUIRE(h.feed({byte}).has_value());

    REQUIRE(h.received.size() == 1);
    CHECK(h.received[0].type == contract_frame_type_v<be::Ready>);
    const std::vector<Frame> sent = h.sent();
    REQUIRE(sent.size() == 2);
    CHECK(sent[1].type == contract_frame_type_v<common::Pong>);
    Result<common::Pong> pong = decode_contract<common::Pong>(sent[1].payload);
    REQUIRE(pong.has_value());
    CHECK(pong->nonce == 7);
}

TEST_CASE("send is dropped until the channel is open", "[process][channel]") {
    Harness h;
    h.channel.send(common::Ping{1});
    CHECK(h.written.empty());
}

TEST_CASE("an oversized frame closes the channel", "[process][channel]") {
    Harness h;
    REQUIRE(h.feed(hello(be::kBackendProtocol)).has_value());
    sb::wire::Writer writer;
    writer.quic_varint(contract_frame_type_v<be::Ready>);
    writer.quic_varint(kChildFrameCap + 1);
    Result<void> fed = h.channel.feed(writer.take());
    REQUIRE_FALSE(fed.has_value());
    CHECK(fed.error().id == "process.child_frame_too_large");
    CHECK(h.channel.phase() == ChannelPhase::Closed);
}
