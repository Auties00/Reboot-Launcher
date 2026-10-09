#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include "host_test_support.hpp"
#include "reboot/host/host_port_allocator.hpp"
#include "reboot/net/port_owner_service.hpp"
#include "reboot/net/port_preflight.hpp"
#include "reboot/testing/fake_port_inspector.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

using namespace rb;
using namespace rb::host;
namespace asio = boost::asio;

namespace {

SessionId session_id(u8 seed) {
    SessionId id;
    id.value.bytes.fill(seed);
    id.value.bytes[6] = 0x41;
    return id;
}

// A UDP wildcard socket on an OS-chosen port with room above it, where the ports are most likely free.
asio::ip::udp::socket bind_with_room(asio::io_context& io) {
    while (true) {
        asio::ip::udp::socket socket(io, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));
        if (socket.local_endpoint().port() <= 65000) return socket;
    }
}

struct Fixture {
    Fixture() : holder(bind_with_room(io)), held{holder.local_endpoint().port()} {}
    ~Fixture() { workers.shutdown(); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    [[nodiscard]] Result<PortBlock> reserve(BlockRequest request, CancelToken token = {}) {
        std::optional<Result<PortBlock>> result;
        Result<void> queued = allocator.reserve(std::move(request), std::move(token),
                                                [&result](Result<PortBlock> block) { result = std::move(block); });
        if (!queued) return std::unexpected(std::move(queued.error()));
        strand.run_until([&] { return result.has_value(); });
        return std::move(*result);
    }

    [[nodiscard]] Port above(u16 offset) const { return Port{static_cast<u16>(held.value + offset)}; }

    [[nodiscard]] AutoPorts range(u16 first_offset, u16 last_offset) const {
        return AutoPorts{PortRange{above(first_offset), above(last_offset)}};
    }

    ManualClock clock;
    test::TestStrand strand{clock};
    asio::io_context io;
    asio::ip::udp::socket holder;
    Port held;
    testing::FakePortInspector inspector;
    testing::ScriptedProcessLauncher processes{strand, clock, testing::FakeOs::Windows};
    net::PortOwnerService owners{inspector, processes};
    net::PortPreflight preflight{io, owners};
    WorkerPool workers{1};
    HostPortAllocator allocator{preflight, workers, strand};
};

}  // namespace

TEST_CASE("Auto takes the lowest free block and never one another session holds", "[host][ports]") {
    Fixture f;
    const Result<PortBlock> first = f.reserve({.session = session_id(1), .policy = f.range(0, 12), .size = 2});
    REQUIRE(first);
    CHECK(*first == PortBlock{f.above(1), 2});
    CHECK(f.allocator.block(session_id(1)) == *first);

    const Result<PortBlock> second = f.reserve({.session = session_id(2), .policy = f.range(0, 12), .size = 2});
    REQUIRE(second);
    CHECK(*second == PortBlock{f.above(3), 2});

    f.allocator.release(session_id(1));
    CHECK_FALSE(f.allocator.block(session_id(1)));
    const Result<PortBlock> third = f.reserve({.session = session_id(3), .policy = f.range(0, 12), .size = 2});
    REQUIRE(third);
    CHECK(*third == PortBlock{f.above(1), 2});
}

TEST_CASE("concurrent reservations queue and get separate blocks", "[host][ports]") {
    Fixture f;
    std::optional<Result<PortBlock>> a;
    std::optional<Result<PortBlock>> b;
    REQUIRE(f.allocator.reserve({.session = session_id(1), .policy = f.range(1, 12), .size = 3}, {},
                                [&a](Result<PortBlock> block) { a = std::move(block); }));
    REQUIRE(f.allocator.reserve({.session = session_id(2), .policy = f.range(1, 12), .size = 3}, {},
                                [&b](Result<PortBlock> block) { b = std::move(block); }));
    f.strand.run_until([&] { return a && b; });
    REQUIRE(*a);
    REQUIRE(*b);
    CHECK_FALSE(a->value().overlaps(b->value()));
}

TEST_CASE("Auto with `after` searches above the failed block and replaces it", "[host][ports]") {
    Fixture f;
    const PortBlock failed = *f.reserve({.session = session_id(1), .policy = f.range(1, 12), .size = 2});
    const Result<PortBlock> next =
        f.reserve({.session = session_id(1), .policy = f.range(1, 12), .size = 2, .after = failed});
    REQUIRE(next);
    CHECK(next->first.value >= failed.first.value + failed.size);
    CHECK(f.allocator.block(session_id(1)) == *next);

    const Result<PortBlock> none =
        f.reserve({.session = session_id(1), .policy = f.range(1, 4), .size = 2, .after = PortBlock{f.above(3), 2}});
    REQUIRE_FALSE(none);
    CHECK(none.error().id == "host.no_free_block");
}

TEST_CASE("a pinned block on a taken port names the owner and is never moved", "[host][ports]") {
    Fixture f;
    f.inspector.set_udp_owner(f.held, ports::PortOwner{.pid = 4242, .exe = NativePath("C:/other/server.exe")});
    const Result<PortBlock> busy = f.reserve({.session = session_id(1), .policy = PinnedPorts{f.held}, .size = 2});
    REQUIRE_FALSE(busy);
    CHECK(busy.error().id == "net.port_busy");
    CHECK_FALSE(f.allocator.block(session_id(1)));

    const Result<PortBlock> free = f.reserve({.session = session_id(1), .policy = PinnedPorts{f.above(1)}, .size = 2});
    REQUIRE(free);
    const Result<PortBlock> overlap = f.reserve({.session = session_id(2), .policy = PinnedPorts{f.above(2)}, .size = 2});
    REQUIRE_FALSE(overlap);
    CHECK(overlap.error().id == "host.block_in_use");
}

TEST_CASE("blocks that cannot exist fail synchronously", "[host][ports]") {
    Fixture f;
    const auto refused = [&](BlockRequest request) {
        return f.allocator.reserve(std::move(request), {}, [](Result<PortBlock>) { FAIL("done must not run"); });
    };
    CHECK(refused({.session = session_id(1), .policy = f.range(1, 3), .size = 0}).error().id == "host.block_out_of_range");
    CHECK(refused({.session = session_id(1), .policy = PinnedPorts{Port{65535}}, .size = 2}).error().id ==
          "host.block_out_of_range");
    CHECK(refused({.session = session_id(1), .policy = f.range(1, 2), .size = 3}).error().id == "host.block_out_of_range");
    CHECK(refused({.session = session_id(1), .policy = PinnedPorts{Port{3550}}, .size = 2}).error().id ==
          "host.reserved_port");
    CHECK(refused({.session = session_id(1), .policy = PinnedPorts{Port{80}}, .size = 1}).error().id ==
          "host.invalid_port_policy");
    f.strand.run_ready();
}

TEST_CASE("Auto skips every block that covers the backend port", "[host][ports]") {
    Fixture f;
    const Result<PortBlock> none =
        f.reserve({.session = session_id(1), .policy = AutoPorts{PortRange{Port{3550}, Port{3552}}}, .size = 3});
    REQUIRE_FALSE(none);
    CHECK(none.error().id == "host.no_free_block");
    CHECK(none.error().retryable);
}

TEST_CASE("a cancelled reservation completes once with host.cancelled", "[host][ports]") {
    Fixture f;
    CancelSource early;
    early.cancel(CancelReason::User);
    const Result<PortBlock> already = f.reserve({.session = session_id(1), .policy = f.range(1, 12), .size = 1}, early.token());
    REQUIRE_FALSE(already);
    CHECK(already.error().id == "host.cancelled");
    CHECK(already.error().kind == ErrorKind::Cancelled);

    std::optional<Result<PortBlock>> first;
    int second_calls = 0;
    std::optional<Result<PortBlock>> second;
    CancelSource cancel;
    REQUIRE(f.allocator.reserve({.session = session_id(1), .policy = f.range(1, 12), .size = 1}, {},
                                [&first](Result<PortBlock> block) { first = std::move(block); }));
    REQUIRE(f.allocator.reserve({.session = session_id(2), .policy = f.range(1, 12), .size = 1}, cancel.token(),
                                [&](Result<PortBlock> block) {
                                    ++second_calls;
                                    second = std::move(block);
                                }));
    cancel.cancel(CancelReason::User);
    f.strand.run_until([&] { return first && second; });
    REQUIRE(*first);
    REQUIRE_FALSE(*second);
    CHECK(second->error().id == "host.cancelled");
    f.strand.run_ready();
    CHECK(second_calls == 1);
    CHECK_FALSE(f.allocator.block(session_id(2)));
}

TEST_CASE("recheck preflights the block a session already holds", "[host][ports]") {
    Fixture f;
    CHECK_FALSE(f.allocator.recheck(session_id(1), {}, {}, [](Result<PortBlock>) {}));
    const PortBlock block = *f.reserve({.session = session_id(1), .policy = f.range(1, 12), .size = 2});

    std::optional<Result<PortBlock>> again;
    REQUIRE(f.allocator.recheck(session_id(1), {}, {}, [&again](Result<PortBlock> checked) { again = std::move(checked); }));
    f.strand.run_until([&] { return again.has_value(); });
    REQUIRE(*again);
    CHECK(**again == block);

    asio::ip::udp::socket squatter(f.io, asio::ip::udp::endpoint(asio::ip::udp::v4(), block.last().value));
    f.inspector.set_udp_owner(block.last(), ports::PortOwner{.pid = 77, .exe = NativePath("C:/squatter.exe")});
    std::optional<Result<PortBlock>> taken;
    REQUIRE(f.allocator.recheck(session_id(1), {}, {}, [&taken](Result<PortBlock> checked) { taken = std::move(checked); }));
    f.strand.run_until([&] { return taken.has_value(); });
    REQUIRE_FALSE(*taken);
    CHECK(taken->error().id == "net.port_busy");
    CHECK(f.allocator.block(session_id(1)) == block);
}
