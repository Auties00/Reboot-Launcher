#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/api/codec.hpp"
#include "reboot/api/v1/common.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/testing/api_test_client.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/frame_log.hpp"
#include "reboot/testing/memory_stream_pair.hpp"

using namespace rb;
using namespace rb::testing;
namespace ipc = contracts::ipc;

namespace {

struct Engine {
    std::unique_ptr<ports::IByteStream> stream;
    FrameLog frames{kIpcFrameCap};

    explicit Engine(std::unique_ptr<ports::IByteStream> s) : stream(std::move(s)) {
        stream->on_read([this](std::span<const u8> bytes) { (void)frames.feed(bytes); });
    }

    template <ContractMessage T>
    void send(const T& message) {
        stream->write(encode_contract_frame(message));
    }
};

}  // namespace

TEST_CASE("ApiTestClient speaks engine IPC and keeps what the engine sent", "[testing][api_client]") {
    DeterministicRuntime runtime;
    auto [client_end, engine_end] = make_memory_stream_pair(runtime.strand(), {"1000", 1}, {"1000", 2});
    Engine engine(std::move(engine_end));
    ApiTestClient client(std::move(client_end));

    client.hello();
    const u64 call = client.call(ipc::kEngineStatus, api::EnvVar{"name", "value"});
    const u64 start = client.start(ipc::kSessionsStop, api::Bytes{}, true);
    const u64 sub = client.subscribe(api::EventFilter{{api::EventKind::Resync}, std::nullopt, std::nullopt});
    client.ping();
    runtime.run_until_idle();
    const auto hello = engine.frames.last<ipc::Hello>();
    REQUIRE(hello);
    CHECK((*hello)->client_kind == ipc::ClientKind::Test);
    CHECK((*hello)->client_build == VersionStreams::ipc_build);
    CHECK((*hello)->abi_version == (u32{VersionStreams::abi_major} << 16 | VersionStreams::abi_minor));
    CHECK(engine.frames.last<ipc::Call>()->value().method_id == ipc::kEngineStatus);
    CHECK(engine.frames.last<ipc::Start>()->value().detached == true);
    CHECK(engine.frames.last<ipc::Subscribe>()->value().sub_id == sub);
    CHECK(engine.frames.count(contract_frame_type_v<contracts::common::Ping>) == 1);

    engine.send(ipc::HelloAck{"build", 7, 42, {}, {}, ipc::EngineOrigin::OnDemand, ipc::StorageMode::ReadWrite, true,
                              ipc::Compatibility::Full});
    engine.send(ipc::Reply{call, api::encode(api::EnvVar{"answer", "42"}), std::nullopt});
    engine.send(ipc::Started{start, 99});
    api::Outcome outcome;
    outcome.op_id = 99;
    outcome.cancelled = api::CancelReason{};
    engine.send(ipc::OpResult{99, api::encode(outcome)});
    engine.send(ipc::EventBatch{sub, {ipc::WireEvent{1, 7, 3, std::nullopt, 99, {}}}});
    engine.send(ipc::Resync{sub});
    engine.send(ipc::ForegroundHint{4242});
    engine.send(ipc::Goodbye{ipc::GoodbyeReason::Shutdown});
    runtime.run_until_idle();

    CHECK(client.hello_ack()->epoch == 7);
    const auto answer = client.response<api::EnvVar>(call);
    REQUIRE(answer);
    REQUIRE(*answer);
    CHECK((*answer)->value == "42");
    CHECK(client.started_op(start) == 99u);
    CHECK(client.outcome(99)->cancelled.has_value());
    REQUIRE(client.events(sub).size() == 1);
    CHECK(client.events(sub).front().seq == 3);
    CHECK(client.resyncs(sub) == 1);
    CHECK(client.foreground_hints() == std::vector<u32>{4242});
    CHECK(client.goodbye_received() == ipc::GoodbyeReason::Shutdown);
    CHECK(client.received().count(contract_frame_type_v<ipc::Reply>) == 1);
    CHECK_FALSE(client.reply_payload(12345));
}

TEST_CASE("ApiTestClient reports engine errors and malformed replies", "[testing][api_client]") {
    DeterministicRuntime runtime;
    auto [client_end, engine_end] = make_memory_stream_pair(runtime.strand(), {}, {});
    Engine engine(std::move(engine_end));
    ApiTestClient client(std::move(client_end));
    const u64 failing = client.call(ipc::kSessionsList, api::Bytes{});
    const u64 empty = client.call(ipc::kSessionsList, api::Bytes{});
    const u64 undecodable = client.call(ipc::kSessionsList, api::Bytes{});
    runtime.run_until_idle();
    engine.send(ipc::Reply{failing, std::nullopt,
                           contracts::common::to_wire(make_diag(ErrorDomain::Engine, MessageId{"engine.version_mismatch"}))});
    engine.send(ipc::Reply{empty, std::nullopt, std::nullopt});
    engine.send(ipc::Reply{undecodable, api::Bytes{0x0A}, std::nullopt});
    runtime.run_until_idle();
    CHECK(client.reply_payload(failing)->error().id == "engine.version_mismatch");
    CHECK(client.reply_payload(empty)->error().id == "testing.malformed_reply");
    const auto decoded = client.response<api::EnvVar>(undecodable);
    REQUIRE(decoded);
    CHECK_FALSE(*decoded);

    client.goodbye();
    client.close();
    runtime.run_until_idle();
    CHECK(client.closed());
    CHECK(engine.frames.last<ipc::Goodbye>()->value().reason == ipc::GoodbyeReason::Normal);
}
