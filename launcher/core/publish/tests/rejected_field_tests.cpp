#include <catch2/catch_test_macros.hpp>
#include <string>

#include "messages.hpp"
#include "rejected_field.hpp"

using namespace reboot;
using namespace reboot::publish;

TEST_CASE("a BAD_REQUEST message maps to the field it names", "[publish]") {
    CHECK(rejected_field("invalid name") == RejectedField::Name);
    CHECK(rejected_field("invalid max_players") == RejectedField::MaxPlayers);
    CHECK(rejected_field("invalid game_port") == RejectedField::GamePort);
    CHECK(rejected_field("invalid password") == RejectedField::Password);
}

TEST_CASE("any other edge text is an unknown field", "[publish]") {
    CHECK(rejected_field("") == RejectedField::Unknown);
    CHECK(rejected_field("invalid view") == RejectedField::Unknown);
    CHECK(rejected_field("invalid name; visit evil.example") == RejectedField::Unknown);
    CHECK(rejected_field("Invalid name") == RejectedField::Unknown);
}

TEST_CASE("edge_rejected carries the field id, never the edge text", "[publish]") {
    const Diagnostic known = edge_rejected("invalid description");
    CHECK(known.is(msg::kEdgeRejected));
    const Arg* field = known.find_arg("field");
    REQUIRE(field);
    CHECK(std::get<std::string>(*field) == "description");

    const Diagnostic unknown = edge_rejected("free text from the edge");
    const Arg* unknown_field = unknown.find_arg("field");
    REQUIRE(unknown_field);
    CHECK(std::get<std::string>(*unknown_field) == "unknown");
    CHECK_FALSE(unknown.detail);
}
