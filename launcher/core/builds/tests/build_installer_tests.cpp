#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <latch>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "builds_test_support.hpp"
#include "reboot/builds/archive_extractor.hpp"
#include "reboot/builds/build_installer.hpp"
#include "reboot/builds/install_outcome.hpp"
#include "reboot/builds/libarchive_extractor.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/net/resumable_downloader.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "service_fixture.hpp"

// Last: it brings in libarchive, and with it windows.h.
#include "archive_writer.hpp"

using namespace reboot;
using namespace reboot::builds;
using namespace reboot::builds::test;

namespace {

const NativePath kWin64 = NativePath("FortniteGame") / "Binaries" / "Win64";

std::string text_of(const std::vector<u8>& bytes) { return {bytes.begin(), bytes.end()}; }

// A downloadable 12.41 whose files say 12.41, inside a top-level folder as host archives often are.
std::vector<u8> build_zip(std::string_view release = "++Fortnite+Release-12.41-CL-12905909") {
    return make_archive("zip", {{"Fortnite 12.41/", "", true},
                                {"Fortnite 12.41/FortniteGame/Binaries/Win64/FortniteClient-Win64-Shipping.exe",
                                 text_of(make_pe(version_blob(release)))},
                                {"Fortnite 12.41/FortniteGame/Binaries/Win64/FortniteLauncher.exe", "launcher"}});
}

catalog::Catalog catalog_for(u64 archive_size, std::optional<std::array<u8, 32>> sha256) {
    catalog::Catalog catalog = test_catalog();
    for (catalog::CatalogEntry& entry : catalog.entries) {
        entry.archive_size = archive_size;
        entry.sha256 = sha256;
    }
    return catalog;
}

// Writes `files` as if they came from the archive, or fails as told.
class ScriptedExtractor final : public IArchiveExtractor {
public:
    Result<ExtractSummary> extract(const ExtractRequest& request, const CancelToken& token,
                                   UniqueFunction<void(const ExtractProgress&)> on_progress) override {
        requests.push_back(request);
        if (token.cancelled()) return std::unexpected(internal_bug("unexpected cancel"));
        if (failure) return std::unexpected(*failure);
        for (const auto& [path, bytes] : files) write_file(request.destination / path, bytes);
        on_progress(ExtractProgress{.entries_done = files.size(), .bytes_done = 1, .bytes_total = 1});
        entered = true;
        if (hold != nullptr) hold->wait();
        return ExtractSummary{.probe = {}, .entries = files.size(), .bytes = 1, .case_collisions = {}};
    }

    std::vector<std::pair<NativePath, std::vector<u8>>> files;
    std::optional<Diagnostic> failure;
    std::vector<ExtractRequest> requests;
    // Set to keep a finished extraction from returning until the test counts it down.
    std::latch* hold = nullptr;
    std::atomic<bool> entered{false};
};

struct InstallFixture : Services {
    explicit InstallFixture(std::vector<u8> body = build_zip(), bool real_extractor = true,
                            std::optional<std::array<u8, 32>> sha256 = std::nullopt)
        : Services(catalog_for(body.size(), sha256)), archive(std::move(body)) {
        disk.add_volume(ports::VolumeInfo{.mount = dir.path(),
                                          .label = "",
                                          .fs_type = "NTFS",
                                          .free_bytes = u64{1} << 30,
                                          .total_bytes = u64{1} << 31});
        serve(200);
        extractor = real_extractor ? static_cast<IArchiveExtractor*>(&libarchive) : &scripted;
        installer = std::make_unique<BuildInstaller>(
            BuildInstallerDeps{.catalog = catalog,
                               .cl_table = cl_table,
                               .library = library,
                               .downloader = downloader,
                               .extractor = *extractor,
                               .disk = disk,
                               .fs = fs,
                               .workers = workers,
                               .strand = strand,
                               .ops = ops},
            InstallerConfig{.policy = DestinationPolicy::LargestFittingVolume,
                            .data_root_builds_dir = dir.path() / "data" / "builds"});
    }

    ~InstallFixture() { workers.shutdown(); }
    InstallFixture(const InstallFixture&) = delete;
    InstallFixture& operator=(const InstallFixture&) = delete;

    void serve(u32 status) {
        testing::FakeHttpResponse response;
        response.status = status;
        if (status == 200) response.body = archive;
        response.headers = {{"ETag", "\"v1\""}};
        response.chunk_size = 4096;
        response.ranges = true;
        transport.route("GET", std::string(kBuildUrl), std::move(response));
    }

    OpHandle install_at(const NativePath& destination, std::string name = "12.41") {
        Result<OpHandle> handle = installer->start_install(
            InstallRequest{.entry = "12.41", .destination = destination, .name = std::move(name), .select_for = {}},
            DisconnectPolicy::Detached);
        REQUIRE(handle);
        return *handle;
    }

    [[nodiscard]] NativePath staging_of(const NativePath& destination) const {
        return destination.parent_path() / kStagingDirName / destination.filename();
    }

    std::vector<u8> archive;
    testing::FakeHttpTransport transport{strand, clock};
    net::HostTlsMemory tls{{}, nullptr};
    net::HttpClient http{transport, tls, strand, timers, random};
    net::ResumableDownloader downloader{http, disk, fs, workers, strand, timers, clock};
    LibArchiveExtractor libarchive;
    ScriptedExtractor scripted;
    IArchiveExtractor* extractor = nullptr;
    std::unique_ptr<BuildInstaller> installer;
};

}  // namespace

TEST_CASE("an install downloads, extracts, detects and registers in one op") {
    InstallFixture f;
    testing::EventRecorder recorder(f.events);
    const NativePath destination = f.dir.path() / "builds" / "12.41";
    Result<OpHandle> handle = f.installer->start_install(
        InstallRequest{.entry = "12.41", .destination = destination, .name = " Chapter 2 ",
                       .select_for = {support::SupportRole::Play}},
        DisconnectPolicy::Detached);
    REQUIRE(handle);

    const InstallOutcome outcome = f.completed<InstallOutcome>(*handle);
    const auto* build = std::get_if<InstalledBuild>(&outcome);
    REQUIRE(build);
    CHECK(build->name == "Chapter 2");
    CHECK(build->version == version("12.41"));
    CHECK(build->cl == Changelist{12905909});
    CHECK(build->version_source == VersionSource::PeResource);
    CHECK(build->catalog_entry == "12.41");
    CHECK(build->root.filename() == "Fortnite 12.41");
    CHECK(std::filesystem::is_regular_file(build->root / kWin64 / std::string(kShippingExe)));
    CHECK(f.library.selected(support::SupportRole::Play) == build->id);
    CHECK_FALSE(std::filesystem::exists(destination.parent_path() / kStagingDirName));

    // Progress is coalesced on a clock the test holds still, so only the first phase is published.
    recorder.pump();
    const auto progress = recorder.payloads<OpProgressEvent>(EventKind::OpProgress);
    REQUIRE_FALSE(progress.empty());
    CHECK(progress.front()->phase == "preparing");
}

TEST_CASE("one install per destination: the same request joins it, another is busy") {
    InstallFixture f;
    const NativePath destination = f.dir.path() / "12.41";
    const OpHandle first = f.install_at(destination);
    Result<OpHandle> same = f.installer->start_install(
        InstallRequest{.entry = "12.41", .destination = destination / "", .name = "12.41"}, DisconnectPolicy::Detached);
    REQUIRE(same);
    CHECK(same->id() == first.id());
    Result<OpHandle> other = f.installer->start_install(
        InstallRequest{.entry = "12.41", .destination = destination, .name = "other"}, DisconnectPolicy::Detached);
    REQUIRE_FALSE(other);
    CHECK(other.error().id == "builds.destination_busy");
    CHECK(f.installer->start_discard_staging(destination, DisconnectPolicy::Detached).error().id ==
          "builds.destination_busy");
    f.completed<InstallOutcome>(first);
}

TEST_CASE("installs are refused before any op when the request is wrong") {
    InstallFixture f;
    const auto start = [&](std::string entry, NativePath destination, std::string name = "n") {
        return f.installer->start_install(
            InstallRequest{.entry = std::move(entry), .destination = std::move(destination), .name = std::move(name)},
            DisconnectPolicy::Detached);
    };
    CHECK(start("99.99", f.dir.path() / "x").error().id == "catalog.entry_not_found");
    CHECK(start("31.00", f.dir.path() / "x").error().id == "builds.unsupported_version");
    CHECK(start("12.41", "relative").error().id == "builds.path_not_absolute");
    CHECK(start("12.41", f.install.install_dir / "x").error().id == "builds.inside_install_dir");
    CHECK(start("12.41", f.dir.path() / "x", "").error().id == "builds.name_empty");
    CHECK(start("cert", f.dir.path().root_path()).error().id == "builds.destination_not_empty");
}

TEST_CASE("the destination and its volume are checked before the download") {
    InstallFixture f;
    const NativePath full = f.dir.path() / "full";
    write_text(full / "file.txt", "x");
    CHECK(f.failed(f.install_at(full)).id == "builds.destination_not_empty");

    f.disk.clear();
    f.disk.add_volume(ports::VolumeInfo{.mount = f.dir.path(), .fs_type = "NTFS", .free_bytes = 10, .read_only = true});
    CHECK(f.failed(f.install_at(f.dir.path() / "ro")).id == "builds.volume_read_only");

    f.disk.clear();
    f.disk.add_volume(ports::VolumeInfo{.mount = f.dir.path(), .fs_type = "FAT32", .free_bytes = u64{1} << 30});
    const Diagnostic fat = f.failed(f.install_at(f.dir.path() / "fat"));
    CHECK(fat.id == "builds.volume_fat");
    CHECK(arg_text(fat, "fs_type") == "FAT32");

    f.disk.clear();
    f.disk.add_volume(ports::VolumeInfo{.mount = f.dir.path(), .fs_type = "NTFS", .free_bytes = 10});
    const Diagnostic space = f.failed(f.install_at(f.dir.path() / "small"));
    CHECK(space.id == "builds.insufficient_space");
    CHECK(arg_text(space, "needed_bytes") == std::to_string(f.archive.size() + 2000));
    CHECK(f.transport.requests().empty());
}

TEST_CASE("a build the host no longer has is unavailable") {
    InstallFixture f;
    f.serve(404);
    const Diagnostic diag = f.failed(f.install_at(f.dir.path() / "gone"));
    CHECK(diag.id == "builds.build_unavailable");
    CHECK(arg_text(diag, "entry") == "12.41");
    REQUIRE_FALSE(diag.causes.empty());
    CHECK(diag.causes[0].id == "net.download_http_status");
}

TEST_CASE("the published checksum is verified, and a mismatch drops the archive") {
    const std::vector<u8> body = build_zip();
    InstallFixture good(body, true, sha256(body));
    const InstallOutcome outcome = good.completed<InstallOutcome>(good.install_at(good.dir.path() / "summed"));
    CHECK(std::holds_alternative<InstalledBuild>(outcome));

    InstallFixture f(body, true, std::array<u8, 32>{});
    const NativePath destination = f.dir.path() / "summed";
    const Diagnostic diag = f.failed(f.install_at(destination));
    CHECK(diag.id == "builds.checksum_mismatch");
    CHECK_FALSE(std::filesystem::exists(f.staging_of(destination) / "archive"));
    CHECK_FALSE(std::filesystem::exists(destination));
}

TEST_CASE("a bad archive goes, a cancelled extraction keeps it for resume") {
    InstallFixture f(build_zip(), false);
    const NativePath destination = f.dir.path() / "bad";
    f.scripted.failure = make_diag(ErrorDomain::Builds, MessageId{"builds.corrupt_archive"})
                             .arg("path", destination)
                             .arg("archive_entry", "x")
                             .build();
    CHECK(f.failed(f.install_at(destination)).id == "builds.corrupt_archive");
    CHECK_FALSE(std::filesystem::exists(f.staging_of(destination) / "archive"));
    CHECK_FALSE(std::filesystem::exists(f.staging_of(destination) / "content"));

    f.scripted.failure =
        make_diag(ErrorDomain::Builds, MessageId{"builds.cancelled"}).kind(ErrorKind::Cancelled).build();
    const OpHandle cancelled = f.install_at(destination);
    f.wait(cancelled);
    CHECK(std::holds_alternative<Cancelled>(*f.ops.outcome(cancelled.id())));
    CHECK(std::filesystem::exists(f.staging_of(destination) / "archive"));
    CHECK_FALSE(std::filesystem::exists(f.staging_of(destination) / "content"));
    CHECK_FALSE(std::filesystem::exists(destination));

    const auto discard = f.installer->start_discard_staging(destination, DisconnectPolicy::Detached);
    REQUIRE(discard);
    f.completed<void>(*discard);
    CHECK_FALSE(std::filesystem::exists(f.staging_of(destination)));
}

TEST_CASE("extracted files that cannot be registered stay, until deleted") {
    InstallFixture f(build_zip(), false);
    f.scripted.files = {{NativePath("readme.txt"), {'x'}}};
    const NativePath destination = f.dir.path() / "orphan";
    const InstallOutcome outcome = f.completed<InstallOutcome>(f.install_at(destination));
    const auto* left = std::get_if<UnregisteredInstall>(&outcome);
    REQUIRE(left);
    CHECK(left->folder == destination);
    CHECK(left->reason.id == "builds.missing_shipping");
    CHECK(std::filesystem::is_regular_file(destination / "readme.txt"));
    CHECK(f.library.list().empty());

    CHECK(f.installer->start_delete_unregistered(f.dir.path() / "elsewhere", DisconnectPolicy::Detached).error().id ==
          "builds.unknown_install_folder");
    const auto deletion = f.installer->start_delete_unregistered(destination, DisconnectPolicy::Detached);
    REQUIRE(deletion);
    f.completed<void>(*deletion);
    CHECK_FALSE(std::filesystem::exists(destination));
    CHECK(f.installer->start_delete_unregistered(destination, DisconnectPolicy::Detached).error().id ==
          "builds.unknown_install_folder");
}

TEST_CASE("a cancel once the files are extracted keeps them, drops the staging and registers nothing") {
    InstallFixture f(build_zip(), false);
    f.scripted.files = {{kWin64 / std::string(kShippingExe), make_pe(version_blob("++Fortnite+Release-12.41"))}};
    std::latch release(1);
    f.scripted.hold = &release;
    const NativePath destination = f.dir.path() / "late";
    const OpHandle handle = f.install_at(destination);
    // Polled: the last progress post can run before `entered` is set, and nothing posts after it.
    while (!f.scripted.entered.load()) {
        f.strand.run_ready();
        std::this_thread::yield();
    }
    REQUIRE(f.ops.cancel(handle.id(), CancelReason::User));
    release.count_down();

    f.strand.run_until([&] { return !std::filesystem::exists(f.staging_of(destination)); });
    CHECK(std::holds_alternative<Cancelled>(*f.ops.outcome(handle.id())));
    CHECK(std::filesystem::is_regular_file(destination / kWin64 / std::string(kShippingExe)));
    CHECK(f.library.list().empty());
    // The folder is known as unregistered once the cancelled install ended on the strand.
    std::optional<OpHandle> deletion;
    f.strand.run_until([&] {
        Result<OpHandle> started = f.installer->start_delete_unregistered(destination, DisconnectPolicy::Detached);
        if (started) deletion = *started;
        return deletion.has_value();
    });
    f.completed<void>(*deletion);
    CHECK_FALSE(std::filesystem::exists(destination));
}

TEST_CASE("discarding staging never takes a volume root, whose staging path holds every archive") {
    InstallFixture f;
    const Result<OpHandle> root = f.installer->start_discard_staging(f.dir.path().root_path(), DisconnectPolicy::Detached);
    REQUIRE_FALSE(root);
    CHECK(root.error().id == "builds.destination_not_empty");
}

TEST_CASE("files that settle no version register with the catalog's") {
    InstallFixture f(build_zip("no release in here"));
    const InstallOutcome outcome = f.completed<InstallOutcome>(f.install_at(f.dir.path() / "plain"));
    const auto* build = std::get_if<InstalledBuild>(&outcome);
    REQUIRE(build);
    CHECK(build->version == version("12.41"));
    CHECK(build->version_source == VersionSource::Catalog);
}

TEST_CASE("the suggested destination is the largest volume that fits, with every volume's problem") {
    InstallFixture f;
    f.disk.clear();
    const auto volume = [&](std::string name, u64 free, std::string fs_type = "NTFS") {
        return ports::VolumeInfo{.mount = f.dir.path() / name, .fs_type = std::move(fs_type), .free_bytes = free};
    };
    f.disk.add_volume(volume("small", 100));
    f.disk.add_volume(volume("fits", u64{10} << 30));
    ports::VolumeInfo removable = volume("usb", u64{40} << 30);
    removable.removable = true;
    f.disk.add_volume(removable);
    f.disk.add_volume(volume("fat", u64{50} << 30, "vfat"));

    const auto handle = f.installer->start_suggest_destination("12.41", DisconnectPolicy::Detached);
    REQUIRE(handle);
    const DestinationSuggestion suggestion = f.completed<DestinationSuggestion>(*handle);
    CHECK(suggestion.destination == f.dir.path() / "fits" / kVolumeBuildsFolder / "12.41");
    CHECK(suggestion.name == "12.41");
    CHECK(suggestion.required_bytes == f.archive.size() + 2000);
    CHECK(suggestion.required_bytes_complete);
    REQUIRE(suggestion.volumes.size() == 4);
    CHECK(suggestion.volumes[0].problem->id == "builds.insufficient_space");
    CHECK_FALSE(suggestion.volumes[1].problem);
    CHECK(suggestion.volumes[2].problem->id == "builds.volume_removable");
    CHECK(suggestion.volumes[3].problem->id == "builds.volume_fat");

    CHECK(f.installer->start_suggest_destination("nope", DisconnectPolicy::Detached).error().id ==
          "catalog.entry_not_found");
}

TEST_CASE("UnderDataRoot suggests the data root's builds folder") {
    InstallFixture f;
    BuildInstaller installer(BuildInstallerDeps{.catalog = f.catalog,
                                                .cl_table = f.cl_table,
                                                .library = f.library,
                                                .downloader = f.downloader,
                                                .extractor = f.libarchive,
                                                .disk = f.disk,
                                                .fs = f.fs,
                                                .workers = f.workers,
                                                .strand = f.strand,
                                                .ops = f.ops},
                             InstallerConfig{.policy = DestinationPolicy::UnderDataRoot,
                                             .data_root_builds_dir = f.dir.path() / "data" / "builds"});
    const auto handle = installer.start_suggest_destination("cert", DisconnectPolicy::Detached);
    REQUIRE(handle);
    const DestinationSuggestion suggestion = f.completed<DestinationSuggestion>(*handle);
    CHECK(suggestion.destination == f.dir.path() / "data" / "builds" / "3.5");
    REQUIRE(suggestion.volumes.size() == 1);
    CHECK_FALSE(suggestion.volumes[0].problem);
}

TEST_CASE("destroying the installer settles a running install") {
    InstallFixture f;
    const OpHandle handle = f.install_at(f.dir.path() / "dropped");
    f.installer.reset();
    const auto outcome = f.ops.outcome(handle.id());
    REQUIRE(outcome);
    const auto* cancelled = std::get_if<Cancelled>(&*outcome);
    REQUIRE(cancelled);
    CHECK(cancelled->reason == CancelReason::Shutdown);
    // What the workers still post finds nothing to touch.
    f.strand.run_ready();
}

TEST_CASE("a cancelled download leaves the destination to a later install") {
    InstallFixture f;
    const NativePath destination = f.dir.path() / "paused";
    const OpHandle handle = f.install_at(destination);
    f.strand.run_until([&] { return !f.transport.requests().empty(); });
    REQUIRE(f.ops.cancel(handle.id(), CancelReason::User));
    // The same request does not join the cancelled op.
    const Result<OpHandle> joined = f.installer->start_install(
        InstallRequest{.entry = "12.41", .destination = destination, .name = "12.41"}, DisconnectPolicy::Detached);
    REQUIRE_FALSE(joined);
    CHECK(joined.error().id == "builds.destination_busy");
    f.wait(handle);
    const std::optional<ErasedOutcome> outcome = f.ops.outcome(handle.id());
    const auto* cancelled = std::get_if<Cancelled>(&*outcome);
    REQUIRE(cancelled);
    CHECK(cancelled->reason == CancelReason::User);
    CHECK_FALSE(std::filesystem::exists(destination));

    // The destination stays busy until the cancelled work ended; the next install then picks up its staging.
    std::optional<OpHandle> resumed;
    f.strand.run_until([&] {
        Result<OpHandle> started = f.installer->start_install(
            InstallRequest{.entry = "12.41", .destination = destination, .name = "resumed"}, DisconnectPolicy::Detached);
        if (started) resumed = *started;
        else CHECK(started.error().id == "builds.destination_busy");
        return resumed.has_value();
    });
    CHECK(std::holds_alternative<InstalledBuild>(f.completed<InstallOutcome>(*resumed)));
}
