#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "reboot/base/diag.hpp"
#include "reboot/base/secret.hpp"
#include "secret_read_order.hpp"

using namespace rb;
using rb::os_linux::platform::erase_everywhere;
using rb::os_linux::platform::read_newest;
using rb::os_linux::platform::write_newest;

namespace {

using Bytes = std::vector<u8>;

[[nodiscard]] Bytes bytes(std::string_view text) { return {text.begin(), text.end()}; }

// Both backends and the tombstones in memory, logging each call.
struct FakeBackends {
    std::map<std::string, Bytes> file;
    std::map<std::string, Bytes> os;
    std::set<std::string> tombstones;
    bool reachable = true;
    bool store_fails = false;
    bool lookup_fails = false;
    bool clear_fails = false;
    bool tombstone_fails = false;
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
    [[nodiscard]] Result<void> store_file(const std::string& key, std::span<const u8> value) {
        calls.push_back("store_file");
        file[key] = Bytes(value.begin(), value.end());
        return {};
    }
    [[nodiscard]] Result<void> clear_file(const std::string& key) {
        calls.push_back("clear_file");
        file.erase(key);
        return {};
    }
    [[nodiscard]] Result<bool> tombstoned(const std::string& key) {
        calls.push_back("tombstoned");
        return tombstones.contains(key);
    }
    [[nodiscard]] Result<void> write_tombstone(const std::string& key) {
        calls.push_back("write_tombstone");
        if (tombstone_fails) return std::unexpected(failure());
        tombstones.insert(key);
        return {};
    }
    [[nodiscard]] Result<void> clear_tombstone(const std::string& key) {
        calls.push_back("clear_tombstone");
        tombstones.erase(key);
        return {};
    }
    [[nodiscard]] Result<void> clear_os(const std::string& key) {
        calls.push_back("clear_os");
        if (clear_fails) return std::unexpected(failure());
        os.erase(key);
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
    CHECK(backends.calls == std::vector<std::string>{"lookup_file", "store_os", "clear_tombstone", "clear_file"});

    backends.calls.clear();
    CHECK(value_of(read_newest(backends, "index")) == bytes("new index"));
    CHECK(backends.calls == std::vector<std::string>{"lookup_file", "tombstoned", "lookup_os"});
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
    CHECK(backends.calls == std::vector<std::string>{"lookup_file", "tombstoned"});
}

TEST_CASE("an erase that cannot reach the keyring still wins over its copy", "[secret_read_order]") {
    FakeBackends backends;
    backends.os["token"] = bytes("stale");
    backends.file["token"] = bytes("newer");
    backends.reachable = false;

    REQUIRE(erase_everywhere(backends, "token"));
    CHECK(backends.calls == std::vector<std::string>{"write_tombstone", "clear_file"});
    CHECK(backends.tombstones.contains("token"));
    CHECK_FALSE(backends.file.contains("token"));
    CHECK_FALSE(value_of(read_newest(backends, "token")).has_value());

    // Once the keyring is back the erase still wins, and the read clears the copy and the tombstone.
    backends.reachable = true;
    backends.calls.clear();
    CHECK_FALSE(value_of(read_newest(backends, "token")).has_value());
    CHECK(backends.calls == std::vector<std::string>{"lookup_file", "tombstoned", "clear_os", "clear_tombstone"});
    CHECK_FALSE(backends.os.contains("token"));
    CHECK(backends.tombstones.empty());

    backends.calls.clear();
    CHECK_FALSE(value_of(read_newest(backends, "token")).has_value());
    CHECK(backends.calls == std::vector<std::string>{"lookup_file", "tombstoned", "lookup_os"});
}

TEST_CASE("a tombstone stays while the keyring copy cannot be cleared", "[secret_read_order]") {
    FakeBackends backends;
    backends.os["token"] = bytes("stale");
    backends.tombstones.insert("token");
    backends.clear_fails = true;

    CHECK_FALSE(value_of(read_newest(backends, "token")).has_value());
    CHECK(backends.tombstones.contains("token"));
    CHECK(backends.os.contains("token"));
}

TEST_CASE("an erase that reaches the keyring clears the file and any tombstone", "[secret_read_order]") {
    FakeBackends backends;
    backends.os["token"] = bytes("kept");
    backends.file["token"] = bytes("newer");
    backends.tombstones.insert("token");

    REQUIRE(erase_everywhere(backends, "token"));
    CHECK(backends.calls == std::vector<std::string>{"clear_os", "clear_file", "clear_tombstone"});
    CHECK(backends.os.empty());
    CHECK(backends.file.empty());
    CHECK(backends.tombstones.empty());
}

TEST_CASE("an erase whose tombstone cannot be written fails and keeps the file", "[secret_read_order]") {
    FakeBackends backends;
    backends.file["token"] = bytes("kept");
    backends.reachable = false;
    backends.tombstone_fails = true;

    CHECK_FALSE(erase_everywhere(backends, "token"));
    CHECK(backends.file["token"] == bytes("kept"));
    CHECK(value_of(read_newest(backends, "token")) == bytes("kept"));
}

TEST_CASE("a put after an unreachable erase is read back once the keyring returns", "[secret_read_order]") {
    FakeBackends backends;
    backends.os["token"] = bytes("stale");
    backends.reachable = false;
    REQUIRE(erase_everywhere(backends, "token"));

    SECTION("the put also misses the keyring") {
        REQUIRE(write_newest(backends, "token", bytes("fresh")));
        CHECK(backends.file["token"] == bytes("fresh"));
        CHECK(backends.tombstones.empty());
        backends.reachable = true;
        // The file moves into the keyring over the stale copy.
        CHECK(value_of(read_newest(backends, "token")) == bytes("fresh"));
        CHECK(value_of(read_newest(backends, "token")) == bytes("fresh"));
        CHECK(backends.os["token"] == bytes("fresh"));
    }
    SECTION("the put reaches the keyring") {
        backends.reachable = true;
        backends.calls.clear();
        REQUIRE(write_newest(backends, "token", bytes("fresh")));
        CHECK(backends.calls == std::vector<std::string>{"store_os", "clear_tombstone", "clear_file"});
        CHECK(value_of(read_newest(backends, "token")) == bytes("fresh"));
    }
}

TEST_CASE("a put falls back to the file when the keyring goes", "[secret_read_order]") {
    FakeBackends backends;
    backends.store_fails = true;
    REQUIRE(write_newest(backends, "token", bytes("value")));
    CHECK(backends.calls == std::vector<std::string>{"store_os", "store_file", "clear_tombstone"});
    CHECK(backends.file["token"] == bytes("value"));
    CHECK_FALSE(backends.reachable);
}
