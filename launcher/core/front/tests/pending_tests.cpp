#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>

#include "front_core.hpp"

using namespace reboot::front;

TEST_CASE("a pending value reaches its waiter, or the dropper once the waiter gave up", "[front][pending]") {
    boost::asio::io_context io;

    auto kept = std::make_shared<Pending<int>>(io.get_executor());
    std::optional<int> dropped;
    auto deliver = kept->resolver([&dropped](int value) { dropped = value; });
    deliver(3);
    io.run();
    CHECK(kept->settled());
    CHECK(kept->take() == 3);
    CHECK(!dropped);

    io.restart();
    auto abandoned = std::make_shared<Pending<int>>(io.get_executor());
    auto late = abandoned->resolver([&dropped](int value) { dropped = value; });
    abandoned->abandon();
    late(7);
    io.run();
    CHECK(dropped == 7);
    CHECK(!abandoned->take());
}
