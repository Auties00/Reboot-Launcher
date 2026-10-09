#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <utility>

#include "host_test_support.hpp"
#include "reboot/host/host_profile_store.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/in_memory_file_system.hpp"

using namespace reboot;
using namespace reboot::host;

namespace {

struct Fixture {
    explicit Fixture(bool load = true) {
        fs.make_dir(path.parent_path());
        if (load) static_cast<void>(document.load());
    }

    Result<void> flush() {
        std::optional<Result<void>> result;
        document.flush({}, [&result](Result<void> flushed) { result = std::move(flushed); });
        strand.run_until([&result] { return result.has_value(); });
        return *result;
    }

    ManualClock clock;
    test::TestStrand strand{clock};
    testing::InMemoryFileSystem fs;
    // Declared after what its jobs use, so it joins them before those go away.
    WorkerPool workers{1};
    testing::FakeRandom random{5};
    NativePath path = testing::default_fake_root() / "data" / "host-profiles.json";
    storage::DocumentStore<HostProfilesDocument> document{fs, workers, strand, clock, path};
    HostProfileStore store{document, random};
};

HostProfile draft(std::string name) { return new_profile(HostProfileId{}, std::move(name), HostListing::Unlisted); }

}  // namespace

TEST_CASE("ensure_builtin adds the default and auto profiles once", "[host][store]") {
    Fixture f;
    REQUIRE(f.store.ensure_builtin(HostListing::Listed));
    const std::vector<HostProfile> profiles = f.store.list();
    REQUIRE(profiles.size() == 2);
    CHECK(profiles[0].id == kDefaultProfileId);
    CHECK(profiles[0].name == "default");
    CHECK(profiles[0].listing == HostListing::Listed);
    CHECK(std::holds_alternative<AutoPorts>(profiles[0].port));
    CHECK(profiles[1].is_auto());
    CHECK(profiles[1].listing == HostListing::Unlisted);
    CHECK_FALSE(profiles[1].port_mapping);
    CHECK(f.document.get().profiles.size() == 2);

    const u64 revision = f.document.revision();
    REQUIRE(f.store.ensure_builtin(HostListing::Unlisted));
    CHECK(f.document.revision() == revision);
    CHECK(f.store.get(kDefaultProfileId)->listing == HostListing::Listed);
}

TEST_CASE("create assigns the id and revision and keeps names unique", "[host][store]") {
    Fixture f;
    REQUIRE(f.store.ensure_builtin(HostListing::Unlisted));
    HostProfile wanted = draft("Weekend");
    wanted.id = kAutoProfileId;
    wanted.revision = 40;
    const Result<HostProfile> created = f.store.create(std::move(wanted));
    REQUIRE(created);
    CHECK_FALSE(created->is_builtin());
    CHECK(created->revision == 1);
    CHECK(f.store.get(created->id)->name == "Weekend");

    CHECK(f.store.create(draft("WEEKEND")).error().id == "host.profile_name_taken");
    CHECK(f.store.create(draft("Default")).error().id == "host.profile_name_taken");
    CHECK(f.store.create(draft("")).error().id == "host.profile_name_empty");
    HostProfile listed_ok = draft("Listed");
    listed_ok.listing = HostListing::Listed;
    CHECK(f.store.create(std::move(listed_ok)));
    CHECK(f.store.list().size() == 4);
}

TEST_CASE("update refuses a stale revision and bumps the stored one", "[host][store]") {
    Fixture f;
    REQUIRE(f.store.ensure_builtin(HostListing::Unlisted));
    HostProfile profile = *f.store.create(draft("Main"));
    HostProfile stale = profile;

    profile.server_name = "Main server";
    const Result<HostProfile> updated = f.store.update(profile);
    REQUIRE(updated);
    CHECK(updated->revision == 2);
    CHECK(f.store.get(profile.id)->server_name == "Main server");

    stale.description = "lost";
    const Result<HostProfile> refused = f.store.update(stale);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "host.profile_stale");
    CHECK(refused.error().kind == ErrorKind::Conflict);

    HostProfile renamed = *updated;
    renamed.name = "auto";
    CHECK(f.store.update(renamed).error().id == "host.profile_name_taken");
    HostProfile missing = *updated;
    missing.id = test::profile_id(9);
    CHECK(f.store.update(missing).error().id == "host.profile_not_found");
    HostProfile auto_listed = *f.store.get(kAutoProfileId);
    auto_listed.listing = HostListing::Listed;
    CHECK(f.store.update(auto_listed).error().id == "host.auto_profile_listed");
}

TEST_CASE("the built-in profiles cannot be removed", "[host][store]") {
    Fixture f;
    REQUIRE(f.store.ensure_builtin(HostListing::Unlisted));
    const HostProfile profile = *f.store.create(draft("Temp"));
    CHECK(f.store.remove(kDefaultProfileId).error().id == "host.builtin_profile");
    CHECK(f.store.remove(kAutoProfileId).error().id == "host.builtin_profile");
    REQUIRE(f.store.remove(profile.id));
    CHECK(f.store.get(profile.id).error().id == "host.profile_not_found");
    CHECK(f.store.remove(profile.id).error().id == "host.profile_not_found");
}

TEST_CASE("reset unlists every profile and clears the built-in texts", "[host][store]") {
    Fixture f;
    REQUIRE(f.store.ensure_builtin(HostListing::Listed));
    HostProfile user = draft("User");
    user.listing = HostListing::Listed;
    user.server_name = "Kept name";
    user = *f.store.create(std::move(user));
    HostProfile builtin = *f.store.get(kDefaultProfileId);
    builtin.server_name = "Gone";
    builtin.match_end.delay = std::chrono::seconds{60};
    builtin = *f.store.update(builtin);
    const HostProfile untouched = *f.store.get(kAutoProfileId);

    REQUIRE(f.store.reset());
    const HostProfile reset_builtin = *f.store.get(kDefaultProfileId);
    CHECK(reset_builtin.listing == HostListing::Unlisted);
    CHECK(reset_builtin.server_name.empty());
    CHECK(reset_builtin.match_end == MatchEndPolicy{});
    CHECK(reset_builtin.revision == builtin.revision + 1);
    const HostProfile reset_user = *f.store.get(user.id);
    CHECK(reset_user.listing == HostListing::Unlisted);
    CHECK(reset_user.server_name == "Kept name");
    CHECK(f.store.get(kAutoProfileId)->revision == untouched.revision);
}

TEST_CASE("a read-only store keeps the built-in profiles in memory", "[host][store]") {
    Fixture f(false);
    f.fs.write_text(f.path, R"({"schema": 9, "revision": 1, "values": {"profiles": []}})");
    static_cast<void>(f.document.load());
    const Result<void> ensured = f.store.ensure_builtin(HostListing::Unlisted);
    REQUIRE_FALSE(ensured);
    CHECK(ensured.error().id == "storage.read_only");
    CHECK(f.store.list().size() == 2);
    CHECK(f.store.get(kAutoProfileId));
    CHECK(f.store.create(draft("Nope")).error().id == "storage.read_only");
}

TEST_CASE("a hand edit that drops a built-in still lists it and reports the reload", "[host][store]") {
    Fixture f;
    REQUIRE(f.store.ensure_builtin(HostListing::Unlisted));
    REQUIRE(f.store.create(draft("auto")).error().id == "host.profile_name_taken");
    REQUIRE(f.flush());
    int reloads = 0;
    f.store.set_on_reload([&reloads] { ++reloads; });

    f.fs.write_text(f.path, R"({"schema": 1, "revision": 9, "values": {"profiles": [
        {"id": "11111111-1111-4111-8111-111111111111", "name": "Auto"}]}})");
    f.document.refresh();
    REQUIRE(f.flush());
    CHECK(reloads == 1);
    const std::vector<HostProfile> profiles = f.store.list();
    REQUIRE(profiles.size() == 3);
    CHECK(profiles[0].id == kDefaultProfileId);
    CHECK(profiles[1].is_auto());
    CHECK(profiles[1].name == "auto-2");
    CHECK(profiles[2].name == "Auto");
}
