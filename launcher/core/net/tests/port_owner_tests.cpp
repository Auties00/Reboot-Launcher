#include <chrono>
#include <map>
#include <optional>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/net/port_owner_service.hpp"
#include "reboot/net/port_preflight.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/testing/fake_port_inspector.hpp"

using namespace rb;
using namespace rb::net;
using rb::testing::PortInspectorOperation;

namespace {

// is_alive answers from a table keyed by pid; anything else is a test bug.
class FakeProcesses final : public ports::IProcessLauncher {
public:
    Result<std::unique_ptr<ports::ChildProcess>> spawn(const ports::ProcessLaunch&) override {
        return make_diag(ErrorDomain::Process, MessageId{"process.not_supported"}).fail();
    }
    Result<bool> is_alive(u32 pid, std::chrono::system_clock::time_point created) override {
        ++queries;
        if (failing) return make_diag(ErrorDomain::Process, MessageId{"process.query_failed"}).fail();
        const auto it = alive.find(pid);
        return it != alive.end() && it->second == created;
    }
    Result<void> kill(u32, std::chrono::system_clock::time_point) override {
        FAIL_CHECK("PortOwnerService never kills");
        return {};
    }

    std::map<u32, std::chrono::system_clock::time_point> alive;
    bool failing = false;
    int queries = 0;
};

const auto kCreated = std::chrono::system_clock::time_point{std::chrono::seconds{1000}};

ports::PortOwner owner(u32 pid, bool wine = false) {
    ports::PortOwner out;
    out.pid = pid;
    out.exe = NativePath("server.exe");
    out.wine_server = wine;
    return out;
}

}  // namespace

TEST_CASE("owners are classified as ours, foreign, system, Wine or unknown", "[net][ports]") {
    testing::FakePortInspector inspector;
    FakeProcesses processes;
    PortOwnerService service(inspector, processes);
    const Endpoint game{IpAddress::v4(0), Port{7777}};
    const std::vector<OurProcess> ours{{42, kCreated}};

    auto free = service.owners(PortProtocol::Udp, game, ours);
    REQUIRE(free);
    CHECK(free->empty());

    inspector.set_udp_owner(Port{7777}, owner(42));
    processes.alive[42] = kCreated;
    auto mine = service.owners(PortProtocol::Udp, game, ours);
    REQUIRE(mine->size() == 1);
    CHECK(mine->front().owner_class == PortOwnerClass::Ours);
    CHECK(*service.held_by(PortProtocol::Udp, game, ours.front()));

    // The pid was reused by another process.
    processes.alive[42] = kCreated + std::chrono::seconds{5};
    CHECK(service.owners(PortProtocol::Udp, game, ours)->front().owner_class == PortOwnerClass::Foreign);
    CHECK_FALSE(*service.held_by(PortProtocol::Udp, game, ours.front()));

    processes.failing = true;
    auto unknown = service.owners(PortProtocol::Udp, game, ours);
    CHECK(unknown->front().owner_class == PortOwnerClass::Unknown);
    CHECK(unknown->front().lookup_error.has_value());
    processes.failing = false;

    inspector.set_udp_owner(Port{7777}, owner(0));
#ifdef _WIN32
    CHECK(service.owners(PortProtocol::Udp, game, ours)->front().owner_class == PortOwnerClass::System);
    inspector.set_udp_owner(Port{7777}, owner(4));
    CHECK(service.owners(PortProtocol::Udp, game, ours)->front().owner_class == PortOwnerClass::System);
#else
    // POSIX inspectors report another user's process as pid 0.
    CHECK(service.owners(PortProtocol::Udp, game, ours)->front().owner_class == PortOwnerClass::Unknown);
    inspector.set_udp_owner(Port{7777}, owner(4));
    CHECK(service.owners(PortProtocol::Udp, game, ours)->front().owner_class == PortOwnerClass::Foreign);
#endif
    inspector.set_udp_owner(Port{7777}, owner(77, true));
    CHECK(service.owners(PortProtocol::Udp, game, ours)->front().owner_class == PortOwnerClass::WineHost);
    const int queries = processes.queries;
    inspector.set_udp_owner(Port{7777}, owner(99));
    CHECK(service.owners(PortProtocol::Udp, game, ours)->front().owner_class == PortOwnerClass::Foreign);
    CHECK(processes.queries == queries);

    const Endpoint backend{IpAddress::v4(0x7F000001), Port{3551}};
    inspector.set_tcp_owner(backend, owner(42));
    processes.alive[42] = kCreated;
    CHECK(service.owners(PortProtocol::Tcp, backend, ours)->front().owner_class == PortOwnerClass::Ours);

    inspector.faults().fail_next(PortInspectorOperation::TcpOwner, make_diag(ErrorDomain::Platform, MessageId{"platform.io"}).build());
    CHECK_FALSE(service.owners(PortProtocol::Tcp, backend, ours));
    inspector.faults().fail_next(PortInspectorOperation::UdpOwner, make_diag(ErrorDomain::Platform, MessageId{"platform.io"}).build());
    CHECK_FALSE(service.held_by(PortProtocol::Udp, game, ours.front()));
}

TEST_CASE("the preflight tells a taken port from a free one and names its owner", "[net][ports]") {
    boost::asio::io_context io;
    testing::FakePortInspector inspector;
    FakeProcesses processes;
    PortOwnerService owners(inspector, processes);
    PortPreflight preflight(io, owners);
    const IpAddress loopback = IpAddress::v4(0x7F000001);

    boost::asio::ip::udp::socket holder(io, boost::asio::ip::udp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
    const Port taken{holder.local_endpoint().port()};
    const Result<PortAvailability> busy = preflight.test(PortProtocol::Udp, Endpoint{loopback, taken});
    REQUIRE(busy);
    CHECK(*busy == PortAvailability::InUse);

    inspector.set_udp_owner(taken, owner(4242));
    const std::vector<Port> block{taken};
    auto conflict = preflight.require_free(PortProtocol::Udp, loopback, block, {});
    REQUIRE(conflict);
    REQUIRE_FALSE(conflict->has_value());
    CHECK(conflict->error().kind == PortConflictKind::InUse);
    CHECK(conflict->error().bind == Endpoint{loopback, taken});
    REQUIRE(conflict->error().owners.size() == 1);
    CHECK(conflict->error().owners.front().owner.pid == 4242);

    inspector.faults().fail_next(PortInspectorOperation::UdpOwner, make_diag(ErrorDomain::Platform, MessageId{"platform.io"}).build());
    auto unreadable = preflight.require_free(PortProtocol::Udp, loopback, block, {});
    REQUIRE(unreadable);
    CHECK(unreadable->error().owners.empty());
    CHECK(unreadable->error().lookup_error.has_value());

    holder.close();
    CHECK(*preflight.test(PortProtocol::Udp, Endpoint{loopback, taken}) == PortAvailability::Free);
    CHECK(preflight.require_free(PortProtocol::Udp, loopback, block, {})->has_value());

    boost::asio::ip::tcp::acceptor listener(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0), false);
    const Port tcp_taken{listener.local_endpoint().port()};
    CHECK(*preflight.test(PortProtocol::Tcp, Endpoint{loopback, tcp_taken}) == PortAvailability::InUse);
}
