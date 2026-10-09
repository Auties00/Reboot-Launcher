#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "socket_activation.hpp"

using rb::os_linux::ipc::ListenEnvironment;
using rb::os_linux::ipc::read_socket_activation;

TEST_CASE("LISTEN_FDS counts only when LISTEN_PID names this process", "[socket_activation]") {
    const auto mine = read_socket_activation(ListenEnvironment{"4242", "1"}, 4242);
    CHECK(mine.for_us);
    CHECK(mine.count == 1U);

    CHECK_FALSE(read_socket_activation(ListenEnvironment{"4241", "1"}, 4242).for_us);
    CHECK_FALSE(read_socket_activation(ListenEnvironment{std::nullopt, "1"}, 4242).for_us);
    CHECK_FALSE(read_socket_activation(ListenEnvironment{"", "1"}, 4242).for_us);
    CHECK_FALSE(read_socket_activation(ListenEnvironment{"+4242", "1"}, 4242).for_us);
    CHECK_FALSE(read_socket_activation(ListenEnvironment{"4242x", "1"}, 4242).for_us);
}

TEST_CASE("a LISTEN_FDS that is not a plain count is kept as unknown", "[socket_activation]") {
    CHECK(read_socket_activation(ListenEnvironment{"7", "2"}, 7).count == 2U);
    CHECK(read_socket_activation(ListenEnvironment{"7", "0"}, 7).count == 0U);
    CHECK_FALSE(read_socket_activation(ListenEnvironment{"7", "one"}, 7).count);
    CHECK_FALSE(read_socket_activation(ListenEnvironment{"7", "-1"}, 7).count);
    CHECK_FALSE(read_socket_activation(ListenEnvironment{"7", std::nullopt}, 7).count);
    CHECK(read_socket_activation(ListenEnvironment{"7", std::nullopt}, 7).for_us);
}
