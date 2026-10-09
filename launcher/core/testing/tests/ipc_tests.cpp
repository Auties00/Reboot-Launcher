#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/ports/ipc.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/in_memory_ipc.hpp"
#include "reboot/testing/manual_waiter.hpp"
#include "reboot/testing/memory_stream_pair.hpp"
#include "reboot/testing/port_conformance.hpp"

using namespace reboot;
using namespace reboot::testing;
using namespace std::chrono_literals;

namespace {

const ports::PeerIdentity kSelf{"1000", 4242};
const ports::PeerIdentity kOther{"1001", 777};

[[nodiscard]] std::span<const u8> text(std::string_view value) {
    return {reinterpret_cast<const u8*>(value.data()), value.size()};
}

struct Collected {
    std::string bytes;
    int closes = 0;
};

void collect(ports::IByteStream& stream, Collected& into) {
    stream.on_read([&into](std::span<const u8> bytes) { into.bytes.append(bytes.begin(), bytes.end()); });
    stream.on_close([&into] { ++into.closes; });
}

}  // namespace

TEST_CASE("InMemoryIpc passes the IPC suite, other-user checks included", "[testing][conformance][ipc]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);
    InMemoryIpc ipc(runtime.strand(), kSelf);
    int next = 0;

    IpcConformanceSubject subject;
    subject.make_listener = [&ipc] { return ipc.make_listener(); };
    subject.connector = ipc.make_connector();
    subject.make_endpoint = [&next] { return "reboot-engine-test-" + std::to_string(next++); };
    subject.self_user_id = kSelf.user_id;
    subject.connect_as_other_user = [&ipc](const std::string& endpoint) -> Result<std::unique_ptr<ports::IByteStream>> {
        ipc.set_client_identity(kOther);
        auto stream = ipc.make_connector()->connect(endpoint, 1s);
        ipc.set_client_identity(kSelf);
        return stream;
    };
    subject.squat_as_other_user = [&ipc](const std::string& endpoint) -> Result<void> {
        ipc.squat(endpoint, kOther);
        return {};
    };
    const ConformanceReport report = run_ipc_conformance(std::move(subject), {waiter, default_fake_root()});
    INFO(report.describe());
    CHECK(report.passed());
}

TEST_CASE("a memory stream pair delivers in order and closes both ends once", "[testing][ipc]") {
    DeterministicRuntime runtime;
    auto [a, b] = make_memory_stream_pair(runtime.strand(), kOther, kSelf);
    CHECK(a->peer().user_id == kOther.user_id);
    CHECK(b->peer().user_id == kSelf.user_id);

    a->write(text("early"));
    runtime.run_until_idle();
    Collected at_a;
    Collected at_b;
    // Bytes written before a reader exist wait for it.
    collect(*b, at_b);
    collect(*a, at_a);
    a->write(text("-later"));
    b->write(text("back"));
    runtime.run_until_idle();
    CHECK(at_b.bytes == "early-later");
    CHECK(at_a.bytes == "back");

    a->write(text("+last"));
    a->close();
    a->close();
    runtime.run_until_idle();
    CHECK(at_b.bytes == "early-later+last");
    CHECK(at_a.closes == 1);
    CHECK(at_b.closes == 1);
    b->write(text("ignored"));
    runtime.run_until_idle();
    CHECK(at_a.bytes == "back");
}

TEST_CASE("destroying one end closes the other", "[testing][ipc]") {
    DeterministicRuntime runtime;
    auto pair = make_memory_stream_pair(runtime.strand(), kSelf, kSelf);
    Collected at_b;
    collect(*pair.b, at_b);
    pair.a.reset();
    runtime.run_until_idle();
    CHECK(at_b.closes == 1);
}

TEST_CASE("InMemoryIpc keeps the squatting and same-user rules", "[testing][ipc]") {
    DeterministicRuntime runtime;
    InMemoryIpc ipc(runtime.strand(), kSelf);
    auto listener = ipc.make_listener();
    std::vector<std::unique_ptr<ports::IByteStream>> accepted;
    REQUIRE(listener->listen("engine", [&](std::unique_ptr<ports::IByteStream> s) { accepted.push_back(std::move(s)); }));
    CHECK(ipc.listening("engine"));
    CHECK_FALSE(ipc.make_listener()->listen("engine", [](std::unique_ptr<ports::IByteStream>) {}));

    auto connector = ipc.make_connector();
    auto client = connector->connect("engine", 1s);
    REQUIRE(client);
    runtime.run_until_idle();
    REQUIRE(accepted.size() == 1);
    CHECK(accepted[0]->peer().user_id == kSelf.user_id);
    CHECK(ipc.open_connections() == 1);

    Collected at_client;
    collect(**client, at_client);
    ipc.sever_all();
    runtime.run_until_idle();
    CHECK(at_client.closes == 1);
    CHECK(ipc.open_connections() == 0);

    ipc.squat("taken", kOther);
    const auto untrusted = connector->connect("taken", 1s);
    REQUIRE_FALSE(untrusted);
    CHECK(untrusted.error().id == "ipc.endpoint_untrusted");
    CHECK(connector->connect("nobody", 1s).error().kind == ErrorKind::EngineUnavailable);

    listener->close();
    CHECK_FALSE(ipc.listening("engine"));
}

TEST_CASE("listeners and connectors outlive InMemoryIpc and see an empty namespace", "[testing][ipc]") {
    DeterministicRuntime runtime;
    std::unique_ptr<ports::IIpcConnector> connector;
    std::unique_ptr<ports::IIpcListener> listener;
    {
        InMemoryIpc ipc(runtime.strand(), kSelf);
        connector = ipc.make_connector();
        listener = ipc.make_listener();
        REQUIRE(listener->listen("engine", [](std::unique_ptr<ports::IByteStream>) {}));
    }
    CHECK(connector->connect("engine", 1s).error().kind == ErrorKind::EngineUnavailable);
    listener.reset();
}
