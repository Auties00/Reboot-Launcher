#include <catch2/catch_test_macros.hpp>

#include <any>
#include <filesystem>
#include <optional>
#include <variant>

#include "builds_test_support.hpp"
#include "reboot/builds/choose_version_prompt.hpp"
#include "reboot/builds/import_outcome.hpp"
#include "reboot/builds/import_service.hpp"
#include "service_fixture.hpp"

using namespace rb;
using namespace rb::builds;
using namespace rb::builds::test;

namespace {

const NativePath kWin64 = NativePath("FortniteGame") / "Binaries" / "Win64";

struct ImportFixture : Services {
    ImportService importer{ImportServiceDeps{.library = library,
                                             .catalog = catalog,
                                             .cl_table = cl_table,
                                             .fs = fs,
                                             .requests = requests,
                                             .workers = workers,
                                             .strand = strand,
                                             .ops = ops}};

    OpHandle start(ImportRequest request) {
        Result<OpHandle> handle = importer.start_import(std::move(request), DisconnectPolicy::Detached);
        REQUIRE(handle);
        return *handle;
    }

    ImportOutcome outcome(ImportRequest request) { return completed<ImportOutcome>(start(std::move(request))); }

    // Runs the import until it asks for a version.
    UserRequest asked(OpHandle handle) {
        strand.run_until([&] { return !requests.pending().empty() || ops.outcome(handle.id()).has_value(); });
        REQUIRE(requests.pending().size() == 1);
        return requests.pending().front();
    }
};

ImportRequest request(const NativePath& path, std::string name = "imported") {
    return ImportRequest{.path = path, .name = std::move(name)};
}

}  // namespace

TEST_CASE("an import resolves, detects and registers the build at its canonical root") {
    ImportFixture f;
    const NativePath root = f.dir.path() / "Season 1";
    write_build(root, "++Fortnite+Release-9.99", "4.16.0-3700114+++Fortnite+Release-Cert");

    const ImportOutcome outcome = f.outcome(request(root, "season one"));
    const auto* imported = std::get_if<Imported>(&outcome);
    REQUIRE(imported);
    CHECK(imported->build.name == "season one");
    CHECK(imported->build.root == std::filesystem::weakly_canonical(root));
    CHECK(imported->build.version == version("1.7.2"));
    CHECK(imported->build.version_source == VersionSource::ClTable);
    CHECK(imported->build.presence == BuildPresence::Present);
    REQUIRE(f.store.get().builds[0].layout);
    CHECK(f.store.get().builds[0].layout->crash_report_clients.size() == 1);

    // The same folder again, by another spelling, is already there: lexically at once, or by file id.
    const Result<OpHandle> again = f.importer.start_import(request(root / "", "again"), DisconnectPolicy::Detached);
    CHECK((again ? f.failed(*again) : again.error()).id == "builds.already_registered");
}

TEST_CASE("the suggested name is the folder's, made unique") {
    ImportFixture f;
    CHECK(f.importer.suggest_name(f.dir.path() / "7.40" / "") == "7.40");
    const NativePath root = f.dir.path() / "7.40";
    write_build(root, "++Fortnite+Release-7.40");
    f.outcome(request(root, "7.40"));
    CHECK(f.importer.suggest_name(f.dir.path() / "other" / "7.40") == "7.40-1");
}

TEST_CASE("several shipping exes complete with the candidates, and a pick imports") {
    ImportFixture f;
    write_build(f.dir.path() / "pack" / "a", "++Fortnite+Release-7.40");
    write_build(f.dir.path() / "pack" / "b", "++Fortnite+Release-8.51");

    const ImportOutcome outcome = f.outcome(request(f.dir.path() / "pack"));
    const auto* choice = std::get_if<NeedsShippingChoice>(&outcome);
    REQUIRE(choice);
    REQUIRE(choice->candidates.size() == 2);

    ImportRequest picked = request(f.dir.path() / "pack");
    picked.shipping_exe = choice->candidates[1];
    const ImportOutcome second = f.outcome(std::move(picked));
    const auto* imported = std::get_if<Imported>(&second);
    REQUIRE(imported);
    CHECK(imported->build.version == version("8.51"));
}

TEST_CASE("an unsettled version completes with NeedsUserVersion when nobody may be asked") {
    ImportFixture f;
    const NativePath root = f.dir.path() / "unknown";
    write_build(root, "++Fortnite+Release-Main");
    ImportRequest quiet = request(root);
    quiet.ask_user = false;
    const ImportOutcome outcome = f.outcome(std::move(quiet));
    const auto* needs = std::get_if<NeedsUserVersion>(&outcome);
    REQUIRE(needs);
    CHECK(needs->raw == "Main");
    CHECK(f.library.list().empty());
}

TEST_CASE("ChooseVersion asks, refuses a version above the cap and imports the answer") {
    ImportFixture f;
    const NativePath root = f.dir.path() / "unknown";
    write_build(root, "++Fortnite+Release-Main");
    const OpHandle handle = f.start(request(root));
    const UserRequest asked = f.asked(handle);
    CHECK(asked.kind == UserRequestKind::ChooseVersion);
    CHECK(asked.op == handle.id());
    const auto* prompt = std::any_cast<ChooseVersionPrompt>(&asked.payload);
    REQUIRE(prompt);
    CHECK(prompt->raw == "Main");
    CHECK(prompt->name == "imported");

    const Result<void> too_new = f.requests.respond(asked.id, UserVersion{.version = version("30.11")});
    REQUIRE_FALSE(too_new);
    CHECK(too_new.error().id == "builds.unsupported_version");
    CHECK(f.requests.pending().size() == 1);

    REQUIRE(f.requests.respond(asked.id, UserVersion{.version = version("12.41"), .cl = Changelist{12905909}}));
    const auto outcome = f.completed<ImportOutcome>(handle);
    const auto* imported = std::get_if<Imported>(&outcome);
    REQUIRE(imported);
    CHECK(imported->build.version == version("12.41"));
    CHECK(imported->build.cl == Changelist{12905909});
    CHECK(imported->build.version_source == VersionSource::User);
}

TEST_CASE("cancelling while the version is asked withdraws the request") {
    ImportFixture f;
    const NativePath root = f.dir.path() / "unknown";
    write_build(root, "++Fortnite+Release-Main");
    const OpHandle handle = f.start(request(root));
    const UserRequest asked = f.asked(handle);
    REQUIRE(f.ops.cancel(handle.id(), CancelReason::User));
    f.strand.run_ready();
    CHECK(f.requests.pending().empty());
    const auto outcome = f.ops.outcome(handle.id());
    REQUIRE(outcome);
    CHECK(std::holds_alternative<Cancelled>(*outcome));
    CHECK_FALSE(f.requests.respond(asked.id, UserVersion{.version = version("12.41")}));
    CHECK(f.library.list().empty());
}

TEST_CASE("a cancel that lands right after the answer registers nothing") {
    ImportFixture f;
    const NativePath root = f.dir.path() / "unknown";
    write_build(root, "++Fortnite+Release-Main");
    const OpHandle handle = f.start(request(root));
    const UserRequest asked = f.asked(handle);
    REQUIRE(f.requests.respond(asked.id, UserVersion{.version = version("12.41")}));
    REQUIRE(f.ops.cancel(handle.id(), CancelReason::User));
    f.strand.run_ready();
    const auto outcome = f.ops.outcome(handle.id());
    REQUIRE(outcome);
    CHECK(std::holds_alternative<Cancelled>(*outcome));
    CHECK(f.library.list().empty());
}

TEST_CASE("a stated version wins, and the catalog entry stands in when the files say nothing") {
    ImportFixture f;
    const NativePath stated_root = f.dir.path() / "stated";
    write_build(stated_root, "++Fortnite+Release-8.51");
    ImportRequest stated = request(stated_root, "stated");
    stated.version = UserVersion{.version = version("8.50")};
    const auto stated_outcome = f.outcome(std::move(stated));
    const auto* stated_import = std::get_if<Imported>(&stated_outcome);
    REQUIRE(stated_import);
    CHECK(stated_import->build.version == version("8.50"));
    CHECK(stated_import->build.version_source == VersionSource::User);

    const NativePath fallback_root = f.dir.path() / "fallback";
    write_build(fallback_root, "nothing here");
    ImportRequest fallback = request(fallback_root, "fallback");
    fallback.catalog_entry = "12.41";
    const auto fallback_outcome = f.outcome(std::move(fallback));
    const auto* fallback_import = std::get_if<Imported>(&fallback_outcome);
    REQUIRE(fallback_import);
    CHECK(fallback_import->build.version == version("12.41"));
    CHECK(fallback_import->build.version_source == VersionSource::Catalog);
    CHECK(fallback_import->build.catalog_entry == "12.41");
}

TEST_CASE("imports are refused synchronously or fail with the reason") {
    ImportFixture f;
    CHECK(f.importer.start_import(request("relative"), DisconnectPolicy::Detached).error().id ==
          "builds.path_not_absolute");
    CHECK(f.importer.start_import(request(f.dir.path() / "x", " "), DisconnectPolicy::Detached).error().id ==
          "builds.name_empty");
    ImportRequest capped = request(f.dir.path() / "x");
    capped.version = UserVersion{.version = version("30.11")};
    CHECK(f.importer.start_import(capped, DisconnectPolicy::Detached).error().id == "builds.unsupported_version");
    ImportRequest unknown_entry = request(f.dir.path() / "x");
    unknown_entry.catalog_entry = "99.99";
    CHECK(f.importer.start_import(unknown_entry, DisconnectPolicy::Detached).error().id == "catalog.entry_not_found");

    CHECK(f.failed(f.start(request(f.dir.path() / "missing"))).id == "builds.path_missing");
    write_text(f.dir.path() / "file", "x");
    CHECK(f.failed(f.start(request(f.dir.path() / "file"))).id == "builds.not_a_directory");
    std::filesystem::create_directories(f.dir.path() / "empty");
    CHECK(f.failed(f.start(request(f.dir.path() / "empty"))).id == "builds.missing_shipping");

    const NativePath too_new = f.dir.path() / "too-new";
    write_build(too_new, "++Fortnite+Release-31.00");
    CHECK(f.failed(f.start(request(too_new))).id == "builds.unsupported_version");
    CHECK(f.library.list().empty());
}
