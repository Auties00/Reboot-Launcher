#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/secret.hpp"
#include "secret_read_order.hpp"

using namespace rb;
using rb::os_linux::platform::read_newest;

namespace {

using Bytes = std::vector<u8>;

[[nodiscard]] Bytes bytes(std::string_view text) { return {text.begin(), text.end()}; }

// Both backends in memory, logging each call.
struct FakeBackends {
    std::map<std::string, Bytes> file;
    std::map<std::string, Bytes> os;
    bool reachable = true;
    bool store_fails = false;
    bool lookup_fails = false;
    int probes = 0;
    std::vector<std::string> calls;

    [[nodiscard]] Diagnostic failure() const {
        return make_diag(ErrorDomain::Platform, MessageId{"platform.test_secret_failure"}).build();
    }

    [[nodiscard]] Result<std::optional<SecretBytes>> lookup_file(const std::string& key) {
        calls.push_back("lookup_file");
        const auto found = file.find(key);
        if (found == file.end()) return std::nullopt;
        return SecretBytes{found->second};
    }
    [[nodiscard]] Result<void> clear_file(const std::string& key) {
        calls.push_back("clear_file");
        file.erase(key);
        return {};
    }
    [[nodiscard]] Result<std::optional<SecretBytes>> lookup_os(const std::string& key) {
        calls.push_back("lookup_os");
        if (lookup_fails) return std::unexpected(failure());
        const auto found = os.find(key);
        if (found == os.end()) return std::nullopt;
        return SecretBytes{found->second};
    }
    [[nodiscard]] Result<void> store_os(const std::string& key, std::span<const u8> value) {
        calls.push_back("store_os");
        if (store_fails) return std::unexpected(failure());
        os[key] = Bytes(value.begin(), value.end());
        return {};
    }
    [[nodiscard]] bool os_active() const { return reachable; }
    [[nodiscard]] bool os_reachable() {
        ++probes;
        return reachable;
    }
    [[nodiscard]] bool fell_back(const Diagnostic&) {
        reachable = false;
        return true;
    }
};

[[nodiscard]] std::optional<Bytes> value_of(const Result<std::optional<SecretBytes>>& read) {
    REQUIRE(read.has_value());
    if (!*read) return std::nullopt;
    return (*read)->reveal();
}

}  // namespace

TEST_CASE("a fallback file wins over an older keyring copy and moves into the keyring", "[secret_read_order]") {
    FakeBackends backends;
    backends.os["index"] = bytes("old index");
    backends.file["index"] = bytes("new index");

    CHECK(value_of(read_newest(backends, "index")) == bytes("new index"));
    CHECK(backends.os["index"] == bytes("new index"));
    CHECK_FALSE(backends.file.contains("index"));
    CHECK(backends.calls == std::vector<std::string>{"lookup_file", "store_os", "clear_file"});

    backends.calls.clear();
    CHECK(value_of(read_newest(backends, "index")) == bytes("new index"));
    CHECK(backends.calls == std::vector<std::string>{"lookup_file", "lookup_os"});
}

TEST_CASE("a file value is returned and kept when the keyring cannot take it", "[secret_read_order]") {
    FakeBackends backends;
    backends.os["token"] = bytes("stale");
    backends.file["token"] = bytes("fresh");

    SECTION("the keyring is unreachable") {
        backends.reachable = false;
        CHECK(value_of(read_newest(backends, "token")) == bytes("fresh"));
        CHECK(backends.calls == std::vector<std::string>{"lookup_file"});
        // A file hit in File mode does not probe the bus.
        CHECK(backends.probes == 0);
    }
    SECTION("the store fails") {
        backends.store_fails = true;
        CHECK(value_of(read_newest(backends, "token")) == bytes("fresh"));
        CHECK(backends.calls == std::vector<std::string>{"lookup_file", "store_os"});
    }
    CHECK(backends.file["token"] == bytes("fresh"));
    CHECK(backends.os["token"] == bytes("stale"));
}

TEST_CASE("a file miss reads the keyring while it answers", "[secret_read_order]") {
    FakeBackends backends;
    backends.os["token"] = bytes("kept");
    CHECK(value_of(read_newest(backends, "token")) == bytes("kept"));
    CHECK_FALSE(value_of(read_newest(backends, "absent")).has_value());

    backends.lookup_fails = true;
    CHECK_FALSE(value_of(read_newest(backends, "token")).has_value());
    CHECK_FALSE(backends.reachable);

    backends.calls.clear();
    CHECK_FALSE(value_of(read_newest(backends, "token")).has_value());
    CHECK(backends.calls == std::vector<std::string>{"lookup_file"});
}
