#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "messages.hpp"
#include "reboot/identity/backend_logins_document.hpp"
#include "reboot/storage/document_store.hpp"

using namespace reboot;
using namespace reboot::identity;
namespace json = boost::json;

static_assert(storage::Document<BackendLoginsDocument>);

namespace {

BackendLoginsDocument read(std::string_view text, std::vector<storage::ValueIssue>& issues) {
    return BackendLoginsDocument::read(json::parse(text).as_object(), issues);
}

}  // namespace

TEST_CASE("backend logins round-trip and keep unknown members", "[identity]") {
    BackendLoginsDocument document;
    document.logins.push_back(BackendLogin{HostPort{"example.com", Port{3551}}, "bob@example.com",
                                           CredentialPolicy::Ticket});
    document.logins.push_back(BackendLogin{HostPort{"::1", Port{8080}}, std::nullopt, CredentialPolicy::LegacyArgv});
    // Unknown members alone keep an otherwise default entry.
    document.logins.push_back(BackendLogin{HostPort{"c.example", Port{3551}}, std::nullopt, CredentialPolicy::Ticket,
                                           json::object{{"future_entry", "x"}}});
    document.unknown.emplace("future", 1);

    std::vector<storage::ValueIssue> issues;
    const BackendLoginsDocument read_back = BackendLoginsDocument::read(document.write(), issues);
    CHECK(issues.empty());
    CHECK(read_back.logins == document.logins);
    CHECK(read_back.unknown == document.unknown);
}

TEST_CASE("an entry equal to the defaults is neither written nor read", "[identity]") {
    BackendLoginsDocument document;
    document.logins.push_back(BackendLogin{HostPort{"example.com", Port{3551}}, std::nullopt, CredentialPolicy::Ticket});
    CHECK(document.write().at("logins").as_array().empty());

    std::vector<storage::ValueIssue> issues;
    const BackendLoginsDocument read_back =
        read(R"({"logins": [{"endpoint": {"host": "example.com", "port": 3551}, "policy": "ticket"}]})", issues);
    CHECK(issues.empty());
    CHECK(read_back.logins.empty());
}

TEST_CASE("an endpoint is read as BackendTarget::normalize leaves it", "[identity]") {
    std::vector<storage::ValueIssue> issues;
    const BackendLoginsDocument document = read(R"({"logins": [{"endpoint": {"host": " Example.COM "}, "login": "bob"}]})", issues);
    CHECK(issues.empty());
    REQUIRE(document.logins.size() == 1);
    CHECK(document.logins[0].endpoint == HostPort{"example.com", Port{3551}});
    CHECK(document.logins[0].login == "bob");
    CHECK(document.logins[0].policy == CredentialPolicy::Ticket);
}

TEST_CASE("bad entries are dropped with a ValueIssue each", "[identity]") {
    std::vector<storage::ValueIssue> issues;
    const BackendLoginsDocument document = read(R"({"logins": [
        {"login": "no-endpoint"},
        42,
        {"endpoint": {"host": "bad host!"}, "login": "bob"},
        {"endpoint": {"host": "a.example", "port": 0}, "login": "bob"},
        {"endpoint": {"host": "b.example"}, "login": ""},
        {"endpoint": {"host": "c.example"}, "login": 7},
        {"endpoint": {"host": "d.example"}, "login": "dee", "policy": "plaintext"},
        {"endpoint": {"host": "e.example"}, "login": "first"},
        {"endpoint": {"host": "E.example", "port": 3551}, "login": "second"}
    ]})", issues);

    REQUIRE(document.logins.size() == 2);
    CHECK(document.logins[0] == BackendLogin{HostPort{"d.example", Port{3551}}, "dee", CredentialPolicy::Ticket});
    CHECK(document.logins[1].login == "first");
    REQUIRE(issues.size() == 8);
    CHECK(issues[0].path == "logins[0]");
    CHECK(issues[0].reason.is(msg::kBackendLoginWithoutEndpoint));
    CHECK(issues[1].reason.is(msg::kBackendLoginWithoutEndpoint));
    CHECK(issues[2].reason.id == "storage.invalid_host");
    CHECK(issues[3].reason.id == "storage.out_of_range");
    CHECK(issues[4].reason.is(msg::kEmptyRemoteLogin));
    CHECK(issues[5].reason.id == "storage.wrong_type");
    CHECK(issues[6].path == "logins[6].policy");
    CHECK(issues[6].reason.id == "storage.unknown_name");
    CHECK(issues[7].path == "logins[8]");
    CHECK(issues[7].reason.is(msg::kDuplicateBackendLogin));
}

TEST_CASE("logins that are not a list read as none", "[identity]") {
    std::vector<storage::ValueIssue> issues;
    const BackendLoginsDocument document = read(R"({"logins": {"host": "example.com"}})", issues);
    CHECK(document.logins.empty());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "logins");
    CHECK(issues[0].reason.is(msg::kBackendLoginsNotList));
}

TEST_CASE("null logins read as none, without an issue", "[identity]") {
    std::vector<storage::ValueIssue> issues;
    const BackendLoginsDocument document = read(R"({"logins": null})", issues);
    CHECK(document.logins.empty());
    CHECK(issues.empty());
}
