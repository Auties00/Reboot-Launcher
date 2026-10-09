#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <catch2/catch_test_macros.hpp>
#include <span>
#include <string>
#include <string_view>

#include "reboot/front/ticket_exchange.hpp"

using namespace rb;
using namespace rb::front;

namespace {

constexpr std::string_view kTicket = "t1cket-T0KEN";

TicketExchange exchange(TicketBinding binding) {
    return TicketExchange(SecretString{std::string(kTicket)},
                          UpstreamLogin{"player@example.com", SecretString{"p&ss w"}}, binding);
}

std::span<const u8> bytes(std::string_view text) { return {reinterpret_cast<const u8*>(text.data()), text.size()}; }

std::string text(const SecretBytes& body) { return {body.reveal().begin(), body.reveal().end()}; }

const std::string kTicketGrant = "grant_type=password&username=ignored&password=t1cket-T0KEN&token_type=eg1";

}  // namespace

TEST_CASE("only POST to the token endpoint applies", "[front][ticket]") {
    CHECK(TicketExchange::applies("POST", "/account/api/oauth/token"));
    CHECK(TicketExchange::applies("POST", "/account/api/oauth/token?x=1"));
    CHECK(!TicketExchange::applies("GET", "/account/api/oauth/token"));
    CHECK(!TicketExchange::applies("POST", "/account/api/oauth/verify"));
}

TEST_CASE("the ticket grant is re-encoded with the stored login", "[front][ticket]") {
    auto tickets = exchange(TicketBinding::Bound);
    const TicketSwap swap = tickets.swap(bytes(kTicketGrant), PeerUser::Unchecked);
    REQUIRE(swap.outcome == TicketSwapOutcome::Swapped);
    CHECK(text(swap.body) == "grant_type=password&username=player%40example.com&password=p%26ss+w&token_type=eg1");
}

TEST_CASE("a grant without a username gets one", "[front][ticket]") {
    auto tickets = exchange(TicketBinding::Bound);
    const TicketSwap swap = tickets.swap(bytes("grant_type=password&password=t1cket-T0KEN"), PeerUser::Engine);
    REQUIRE(swap.outcome == TicketSwapOutcome::Swapped);
    CHECK(text(swap.body) == "grant_type=password&password=p%26ss+w&username=player%40example.com");
}

TEST_CASE("bodies without the ticket pass through", "[front][ticket]") {
    auto tickets = exchange(TicketBinding::Unbound);
    for (const char* body :
         {"grant_type=exchange_code&exchange_code=abc", "grant_type=password&password=other",
          "grant_type=password&password=t1cket-T0KEN&password=t1cket-T0KEN", "grant_type=password&password=%zz",
          "grant_type=refresh_token&refresh_token=t1cket-T0KEN"})
        CHECK(tickets.swap(bytes(body), PeerUser::Other).outcome == TicketSwapOutcome::PassedThrough);
    CHECK(tickets.state() == TicketState::Available);
}

TEST_CASE("another user's socket is refused", "[front][ticket][peer]") {
    auto tickets = exchange(TicketBinding::Bound);
    CHECK(tickets.swap(bytes(kTicketGrant), PeerUser::Other).outcome == TicketSwapOutcome::Refused);
}

TEST_CASE("an unbound ticket is reserved, so concurrent grants cannot both swap", "[front][ticket][unbound]") {
    auto tickets = exchange(TicketBinding::Unbound);
    CHECK(tickets.swap(bytes(kTicketGrant), PeerUser::Unchecked).outcome == TicketSwapOutcome::Swapped);
    CHECK(tickets.state() == TicketState::Reserved);
    CHECK(tickets.swap(bytes(kTicketGrant), PeerUser::Unchecked).outcome == TicketSwapOutcome::Refused);

    tickets.settle(401);
    CHECK(tickets.state() == TicketState::Available);
    CHECK(tickets.swap(bytes(kTicketGrant), PeerUser::Unchecked).outcome == TicketSwapOutcome::Swapped);
    tickets.settle(std::nullopt);
    CHECK(tickets.state() == TicketState::Available);

    CHECK(tickets.swap(bytes(kTicketGrant), PeerUser::Unchecked).outcome == TicketSwapOutcome::Swapped);
    tickets.settle(200);
    CHECK(tickets.state() == TicketState::Spent);
    CHECK(tickets.swap(bytes(kTicketGrant), PeerUser::Unchecked).outcome == TicketSwapOutcome::Refused);
}

TEST_CASE("a bound ticket may log in again", "[front][ticket][bound]") {
    auto tickets = exchange(TicketBinding::Bound);
    CHECK(tickets.swap(bytes(kTicketGrant), PeerUser::Engine).outcome == TicketSwapOutcome::Swapped);
    CHECK(tickets.swap(bytes(kTicketGrant), PeerUser::Engine).outcome == TicketSwapOutcome::Swapped);
    tickets.settle(200);
    CHECK(tickets.state() == TicketState::Available);
    CHECK(tickets.swap(bytes(kTicketGrant), PeerUser::Engine).outcome == TicketSwapOutcome::Swapped);
}

TEST_CASE("the refused-grant body is Epic's invalid-credentials error", "[front][ticket]") {
    STATIC_CHECK(kRefusedGrantStatus == 400);
    boost::system::error_code error;
    const boost::json::value body = boost::json::parse(kRefusedGrantBody, error);
    REQUIRE(!error);
    CHECK(body.at("errorCode").as_string() == kRefusedGrantErrorName);
    CHECK(std::to_string(body.at("numericErrorCode").as_int64()) == kRefusedGrantErrorCode);
    CHECK(body.at("error").as_string() == "invalid_grant");
}
