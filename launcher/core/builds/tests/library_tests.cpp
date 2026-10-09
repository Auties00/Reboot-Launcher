#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "builds_test_support.hpp"
#include "reboot/builds/library_changed_event.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "service_fixture.hpp"

using namespace reboot;
using namespace reboot::builds;
using namespace reboot::builds::test;
using namespace std::chrono_literals;

namespace {

const NativePath kWin64 = NativePath("FortniteGame") / "Binaries" / "Win64";

DetectedVersion detected(std::string_view text, VersionSource source = VersionSource::PeResource) {
    return DetectedVersion{.version = version(text), .cl = std::nullopt, .source = source, .raw = std::string(text)};
}

InstalledBuild add(Services& s, std::string name, const NativePath& root, std::optional<DetectedVersion> version = {},
                   std::optional<BuildLayout> layout = std::nullopt) {
    Result<InstalledBuild> added = s.library.add(NewBuild{.name = std::move(name),
                                                          .root = root,
                                                          .version = std::move(version),
                                                          .layout = std::move(layout),
                                                          .catalog_entry = std::nullopt,
                                                          .needs_relocation = false});
    REQUIRE(added);
    return *added;
}

BuildLayout layout_at(const NativePath& root) {
    return BuildLayout{.root = root, .shipping_exe = kWin64 / std::string(kShippingExe)};
}

}  // namespace

TEST_CASE("add keeps a trimmed, unique name and publishes the change") {
    Services s;
    testing::EventRecorder recorder(s.events);
    const InstalledBuild build = add(s, "  Season 7  ", s.dir.path() / "7.40", detected("7.40"));
    CHECK(build.name == "Season 7");
    CHECK(build.version == version("7.40"));
    CHECK(build.version_source == VersionSource::PeResource);
    CHECK(build.version_confirmed());
    CHECK(build.presence == BuildPresence::Unchecked);
    CHECK(s.library.list().size() == 1);
    CHECK(s.library.get(build.id)->name == "Season 7");

    recorder.pump();
    const auto changes = recorder.payloads<LibraryChangedEvent>(EventKind::LibraryChanged);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0]->change == LibraryChange::Added);
    CHECK(changes[0]->build == build.id);

    CHECK(s.library.check_name("SEASON 7", std::nullopt).error().id == "builds.name_taken");
    CHECK(s.library.check_name("season 7", build.id).value() == "season 7");
    CHECK(s.library.check_name(" \t ", std::nullopt).error().id == "builds.name_empty");
    CHECK(s.library.get(BuildId{}).error().id == "builds.not_found");
}

TEST_CASE("roots must be absolute, apart from every other build and outside the install") {
    Services s;
    const NativePath root = s.dir.path() / "builds" / "7.40";
    add(s, "a", root);
    CHECK(s.library.check_root("relative/path", std::nullopt).error().id == "builds.path_not_absolute");
    CHECK(s.library.check_root(root, std::nullopt).error().id == "builds.already_registered");
    CHECK(s.library.check_root(root / "", std::nullopt).error().id == "builds.already_registered");
    CHECK(s.library.check_root(root / "inner", std::nullopt).error().id == "builds.overlaps_build");
    const Diagnostic above = s.library.check_root(s.dir.path() / "builds", std::nullopt).error();
    CHECK(above.id == "builds.overlaps_build");
    CHECK(arg_text(above, "name") == "a");
    CHECK(s.library.check_root(s.install.install_dir / "game", std::nullopt).error().id == "builds.inside_install_dir");
    CHECK(s.library.check_root(s.dir.path() / "builds" / "8.51", std::nullopt));

    const Result<InstalledBuild> again =
        s.library.add(NewBuild{.name = "b", .root = root, .version = {}, .layout = {}, .catalog_entry = {}});
    REQUIRE_FALSE(again);
    CHECK(again.error().id == "builds.already_registered");
}

TEST_CASE("selection is by id, survives renames and clears with the build") {
    Services s;
    const InstalledBuild build = add(s, "a", s.dir.path() / "a");
    CHECK_FALSE(s.library.selected(support::SupportRole::Play));
    REQUIRE(s.library.select(support::SupportRole::Play, build.id));
    REQUIRE(s.library.update(build.id, BuildPatch{.name = "renamed"}));
    CHECK(s.library.selected(support::SupportRole::Play) == build.id);
    CHECK_FALSE(s.library.selected(support::SupportRole::Host));
    CHECK(s.library.select(support::SupportRole::Host, BuildId{}).error().id == "builds.not_found");

    const auto removal = s.library.start_remove(build.id, RemoveFiles::Keep, RunningPolicy::Refuse,
                                                DisconnectPolicy::Detached);
    REQUIRE(removal);
    s.completed<void>(*removal);
    CHECK(s.library.list().empty());
    CHECK_FALSE(s.library.selected(support::SupportRole::Play));
    CHECK_FALSE(s.store.get().client_selection);
}

TEST_CASE("a version set by hand becomes the user's") {
    Services s;
    const InstalledBuild build = add(s, "a", s.dir.path() / "a", detected("12.41"));
    const Result<InstalledBuild> updated = s.library.update(
        build.id, BuildPatch{.name = std::nullopt, .version = version("12.50"), .cl = Changelist{13137020}});
    REQUIRE(updated);
    CHECK(updated->version == version("12.50"));
    CHECK(updated->cl == Changelist{13137020});
    CHECK(updated->version_source == VersionSource::User);
    CHECK(s.library.update(BuildId{}, BuildPatch{}).error().id == "builds.not_found");
    add(s, "b", s.dir.path() / "b");
    CHECK(s.library.update(build.id, BuildPatch{.name = "B"}).error().id == "builds.name_taken");
}

TEST_CASE("compatible builds: the exact version first, then its bucket, newest first") {
    Services s;
    const InstalledBuild exact_old = add(s, "exact-old", s.dir.path() / "1", detected("7.40"));
    s.clock.advance(1h);
    const InstalledBuild exact_new = add(s, "exact-new", s.dir.path() / "2", detected("7.40"));
    const InstalledBuild patch = add(s, "patch", s.dir.path() / "3", detected("7.40.1"));
    add(s, "other", s.dir.path() / "4", detected("7.30"));
    add(s, "unconfirmed", s.dir.path() / "5");

    const std::vector<InstalledBuild> compatible = s.library.find_compatible(version("7.40"));
    REQUIRE(compatible.size() == 3);
    CHECK(compatible[0].id == exact_new.id);
    CHECK(compatible[1].id == exact_old.id);
    CHECK(compatible[2].id == patch.id);
    CHECK(s.library.find_compatible(std::string_view("7.40")).size() == 3);
    CHECK(s.library.find_compatible(std::string_view("Fortnite 7.40")).empty());
}

TEST_CASE("removal handles the files first and keeps the entry when that fails") {
    Services s;
    const NativePath root = s.dir.path() / "doomed";
    write_text(root / "file.txt", "x");
    const InstalledBuild build = add(s, "doomed", root);

    s.fs.fail_removal = true;
    const auto failing = s.library.start_remove(build.id, RemoveFiles::Delete, RunningPolicy::Refuse,
                                                DisconnectPolicy::Detached);
    REQUIRE(failing);
    const Diagnostic diag = s.failed(*failing);
    CHECK(diag.id == "builds.remove_failed");
    CHECK(s.library.list().size() == 1);

    s.fs.fail_removal = false;
    const auto deleting = s.library.start_remove(build.id, RemoveFiles::Delete, RunningPolicy::Refuse,
                                                 DisconnectPolicy::Detached);
    REQUIRE(deleting);
    s.completed<void>(*deleting);
    CHECK_FALSE(std::filesystem::exists(root));
    CHECK(s.library.list().empty());
}

TEST_CASE("trash goes through the shell") {
    Services s;
    const NativePath root = s.dir.path() / "trashed";
    write_text(root / "file.txt", "x");
    const InstalledBuild build = add(s, "t", root);
    const auto removal = s.library.start_remove(build.id, RemoveFiles::Trash, RunningPolicy::Refuse,
                                                DisconnectPolicy::Detached);
    REQUIRE(removal);
    s.completed<void>(*removal);
    CHECK(s.shell.trashed() == std::vector<NativePath>{root});
}

TEST_CASE("a build in use is refused, or its sessions are stopped first") {
    Services s;
    const InstalledBuild build = add(s, "busy", s.dir.path() / "busy");
    s.usage.sessions[build.id] = {SessionId{}, SessionId{}};

    const auto refused = s.library.start_remove(build.id, RemoveFiles::Keep, RunningPolicy::Refuse,
                                                DisconnectPolicy::Detached);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "builds.in_use");
    CHECK(arg_text(refused.error(), "count") == "2");

    s.usage.stop_result = std::unexpected(internal_bug("stop"));
    const auto stop_fails = s.library.start_remove(build.id, RemoveFiles::Keep, RunningPolicy::StopSessions,
                                                   DisconnectPolicy::Detached);
    REQUIRE(stop_fails);
    CHECK(s.failed(*stop_fails).id == "internal.bug");
    CHECK(s.library.list().size() == 1);

    s.usage.stop_result = {};
    const auto stopped = s.library.start_remove(build.id, RemoveFiles::Keep, RunningPolicy::StopSessions,
                                                DisconnectPolicy::Detached);
    REQUIRE(stopped);
    s.completed<void>(*stopped);
    CHECK(s.usage.stop_requests.size() == 2);
    CHECK(s.library.list().empty());
}

TEST_CASE("relocation re-detects at the new root and refuses another version") {
    Services s;
    const NativePath old_root = s.dir.path() / "old";
    const NativePath new_root = s.dir.path() / "new";
    write_build(new_root, "++Fortnite+Release-12.41-CL-12905909");
    const InstalledBuild build = add(s, "moved", old_root, detected("12.41"));

    const auto relocation =
        s.library.start_relocate(build.id, new_root, RunningPolicy::Refuse, DisconnectPolicy::Detached);
    REQUIRE(relocation);
    const InstalledBuild moved = s.completed<InstalledBuild>(*relocation);
    CHECK(moved.root == std::filesystem::weakly_canonical(new_root));
    CHECK(moved.presence == BuildPresence::Present);
    CHECK_FALSE(moved.needs_relocation);
    REQUIRE(s.store.get().builds[0].layout);
    CHECK(s.store.get().builds[0].layout->shipping_exe == kWin64 / std::string(kShippingExe));

    const NativePath other = s.dir.path() / "other";
    write_build(other, "++Fortnite+Release-8.51");
    const auto mismatch = s.library.start_relocate(build.id, other, RunningPolicy::Refuse, DisconnectPolicy::Detached);
    REQUIRE(mismatch);
    const Diagnostic diag = s.failed(*mismatch);
    CHECK(diag.id == "builds.version_mismatch");
    CHECK(arg_text(diag, "found_version") == "8.51");
    CHECK(arg_text(diag, "version") == "12.41");
}

TEST_CASE("relocation gives an unknown version the one the files settle") {
    Services s;
    const NativePath root = s.dir.path() / "found";
    write_build(root, "++Fortnite+Release-8.51");
    const InstalledBuild build = add(s, "lost", s.dir.path() / "lost");
    const auto relocation = s.library.start_relocate(build.id, root, RunningPolicy::Refuse, DisconnectPolicy::Detached);
    REQUIRE(relocation);
    const InstalledBuild moved = s.completed<InstalledBuild>(*relocation);
    CHECK(moved.version == version("8.51"));
    CHECK(moved.version_source == VersionSource::PeResource);

    const auto missing = s.library.start_relocate(build.id, s.dir.path() / "nowhere", RunningPolicy::Refuse,
                                                  DisconnectPolicy::Detached);
    REQUIRE(missing);
    CHECK(s.failed(*missing).id == "builds.io");
}

TEST_CASE("resolve_layout reuses a valid stored layout and marks a vanished build missing") {
    Services s;
    const NativePath root = s.dir.path() / "laid-out";
    write_build(root, "++Fortnite+Release-8.51");
    const InstalledBuild build = add(s, "l", root, detected("8.51"), layout_at(root));
    CHECK(s.library.get(build.id)->presence == BuildPresence::Present);

    std::optional<Result<BuildLayout>> result;
    s.library.resolve_layout(build.id, CancelToken{}, [&](Result<BuildLayout> layout) { result = std::move(layout); });
    s.strand.run_until([&] { return result.has_value(); });
    REQUIRE(*result);
    CHECK((*result)->launcher_exe == std::nullopt);

    std::filesystem::remove_all(root);
    result.reset();
    s.library.resolve_layout(build.id, CancelToken{}, [&](Result<BuildLayout> layout) { result = std::move(layout); });
    s.strand.run_until([&] { return result.has_value(); });
    REQUIRE_FALSE(*result);
    CHECK(s.library.get(build.id)->presence == BuildPresence::Missing);

    result.reset();
    s.library.resolve_layout(BuildId{}, CancelToken{}, [&](Result<BuildLayout> layout) { result = std::move(layout); });
    s.strand.run_until([&] { return result.has_value(); });
    CHECK((*result).error().id == "builds.not_found");
}

TEST_CASE("resolve_layout walks again and stores what it finds") {
    Services s;
    const NativePath root = s.dir.path() / "walked";
    write_build(root, "++Fortnite+Release-8.51");
    const InstalledBuild build = add(s, "w", root);
    std::optional<Result<BuildLayout>> result;
    s.library.resolve_layout(build.id, CancelToken{}, [&](Result<BuildLayout> layout) { result = std::move(layout); });
    s.strand.run_until([&] { return result.has_value(); });
    REQUIRE(*result);
    CHECK((*result)->launcher_exe == kWin64 / std::string(kLauncherExe));
    REQUIRE(s.store.get().builds[0].layout);
    CHECK(s.store.get().builds[0].layout->launcher_exe == kWin64 / std::string(kLauncherExe));
}

TEST_CASE("the version source and layout persist through the store") {
    Services s;
    const NativePath root = s.dir.path() / "kept";
    add(s, "kept", root, detected("1.7.2", VersionSource::ClTable), layout_at(root));
    std::optional<Result<void>> flushed;
    s.store.flush(CancelToken{}, [&](Result<void> r) { flushed = std::move(r); });
    s.strand.run_until([&] { return flushed.has_value(); });
    REQUIRE(*flushed);

    storage::DocumentStore<storage::LibraryDocument> reopened{s.fs, s.workers, s.strand, s.clock,
                                                              s.dir.path() / "library.json"};
    (void)reopened.load();
    REQUIRE(reopened.get().builds.size() == 1);
    const storage::LibraryEntry& entry = reopened.get().builds[0];
    CHECK(entry.version_source == "cl_table");
    REQUIRE(entry.layout);
    CHECK(entry.layout->shipping_exe == kWin64 / std::string(kShippingExe));
}

TEST_CASE("destroying the library settles its running ops") {
    std::optional<Services> s;
    s.emplace();
    const InstalledBuild build = add(*s, "a", s->dir.path() / "a");
    s->usage.sessions[build.id] = {SessionId{}};
    // A stop that never answers keeps the op running.
    struct Silent final : IBuildUsage {
        std::vector<SessionId> sessions_using(BuildId) const override { return {SessionId{}}; }
        void stop_sessions_using(BuildId, UniqueFunction<void(Result<void>)> done) override { held = std::move(done); }
        UniqueFunction<void(Result<void>)> held;
    };
    Silent silent;
    auto library = std::make_unique<Library>(LibraryDeps{.store = s->store,
                                                         .usage = silent,
                                                         .cl_table = s->cl_table,
                                                         .catalog = s->catalog,
                                                         .fs = s->fs,
                                                         .shell = s->shell,
                                                         .workers = s->workers,
                                                         .strand = s->strand,
                                                         .ops = s->ops,
                                                         .events = s->events,
                                                         .clock = s->clock,
                                                         .random = s->random,
                                                         .install = s->install});
    const auto removal = library->start_remove(build.id, RemoveFiles::Keep, RunningPolicy::StopSessions,
                                               DisconnectPolicy::Detached);
    REQUIRE(removal);
    CHECK_FALSE(s->ops.outcome(removal->id()));
    library.reset();
    const auto outcome = s->ops.outcome(removal->id());
    REQUIRE(outcome);
    const auto* cancelled = std::get_if<Cancelled>(&*outcome);
    REQUIRE(cancelled);
    CHECK(cancelled->reason == CancelReason::Shutdown);
    // A late answer from the sessions package reaches nothing.
    silent.held(Result<void>{});
}
