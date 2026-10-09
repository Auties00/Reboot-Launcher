#pragma once

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <chrono>
#include <optional>
#include <type_traits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "builds_test_support.hpp"
#include "reboot/builds/cl_table.hpp"
#include "reboot/builds/library.hpp"
#include "reboot/catalog/catalog.hpp"
#include "reboot/catalog/catalog_service.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/library_document.hpp"
#include "reboot/testing/fake_disk_info.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/fake_shell.hpp"

namespace rb::builds::test {

inline constexpr std::string_view kBuildUrl = "https://builds.test/12.41.zip";

[[nodiscard]] inline catalog::CatalogEntry catalog_entry(std::string id, std::string_view version_text,
                                                        std::string url) {
    catalog::CatalogEntry entry;
    entry.id = std::move(id);
    entry.version = version(version_text);
    entry.url = std::move(url);
    entry.format = catalog::ArchiveFormat::Zip;
    entry.container = catalog::ArchiveContainer::None;
    entry.archive_size = 1000;
    entry.installed_size = 2000;
    entry.availability = catalog::Availability::Available;
    return entry;
}

[[nodiscard]] inline catalog::Catalog test_catalog() {
    catalog::Catalog catalog;
    catalog.schema = catalog::kCatalogSchema;
    catalog.serial = 1;
    catalog.expires_at = std::chrono::system_clock::time_point(std::chrono::hours{24 * 365});
    catalog::CatalogEntry cert = catalog_entry("3.5", "3.5", "https://builds.test/3.5.zip");
    cert.aliases = {"cert"};
    catalog.entries.push_back(std::move(cert));
    catalog.entries.push_back(catalog_entry("12.41", "12.41", std::string(kBuildUrl)));
    catalog.entries.push_back(catalog_entry("31.00", "31.0", "https://builds.test/31.00.zip"));
    return catalog;
}

// The id, args and causes, for a failure message.
[[nodiscard]] inline std::string describe(const Diagnostic& diag) {
    std::string out = diag.id;
    for (const auto& [name, value] : diag.args) out += " " + name + "=" + arg_text(diag, name);
    if (diag.detail) out += " (" + *diag.detail + ")";
    for (const Diagnostic& cause : diag.causes) out += " <- " + describe(cause);
    return out;
}

// The engine's strand machinery on manual time, real workers, and a Library over an in-memory store.
struct Services {
    explicit Services(catalog::Catalog catalog_document = test_catalog())
        : remote(catalog_document, catalog::CatalogOrigin::Cache),
          bundled(std::move(catalog_document), catalog::CatalogOrigin::Bundled) {
        fs.memory.make_dir(dir.path());
        (void)store.load();
        REQUIRE(catalog.start_refresh(catalog::CatalogRefresh::IfExpired, DisconnectPolicy::Detached));
        strand.run_ready();
        REQUIRE(catalog.current().entries.size() == 3);
    }

    // Workers finish what they hold while everything they touch is still alive.
    ~Services() { workers.shutdown(); }
    Services(const Services&) = delete;
    Services& operator=(const Services&) = delete;

    template <class T>
    T completed(OpHandle handle) {
        wait(handle);
        const std::optional<ErasedOutcome> outcome = ops.outcome(handle.id());
        const auto* done = std::get_if<Completed<std::any>>(&*outcome);
        if (done == nullptr) {
            // Fails showing why the op ended otherwise.
            const auto* failure = std::get_if<Failed>(&*outcome);
            const std::string why = failure != nullptr ? describe(failure->error) : std::string("not completed");
            REQUIRE(why == std::string());
        }
        if constexpr (std::is_void_v<T>) return;
        else return std::any_cast<T>(done->value);
    }

    Diagnostic failed(OpHandle handle) {
        wait(handle);
        const std::optional<ErasedOutcome> outcome = ops.outcome(handle.id());
        const auto* failure = std::get_if<Failed>(&*outcome);
        REQUIRE(failure);
        return failure->error;
    }

    void wait(OpHandle handle) {
        strand.run_until([&] { return ops.outcome(handle.id()).has_value(); });
        // Lets the work finish its own completion, so the next step starts from a settled state.
        strand.run_ready();
    }

    testing::ScratchDir dir = make_scratch("reboot-builds-services");
    ManualClock clock;
    TestStrand strand{clock};
    TimerService timers{clock, strand};
    EventBus events{EngineEpoch{1}};
    OpRegistry ops{clock, timers, events};
    UserRequestRegistry requests{events};
    WorkerPool workers{2};
    TreeFileSystem fs;
    testing::FakeShell shell{&fs};
    testing::FakeRandom random{7};
    testing::FakeDiskInfo disk;
    FakeBuildUsage usage;
    ClTable cl_table;
    InstallLayout install{.install_dir = dir.path() / "app"};
    FixedCatalogSource remote;
    FixedCatalogSource bundled;
    catalog::CatalogService catalog{remote, bundled, ops, events, clock};
    storage::DocumentStore<storage::LibraryDocument> store{fs, workers, strand, clock, dir.path() / "library.json"};
    Library library{LibraryDeps{.store = store,
                                .usage = usage,
                                .cl_table = cl_table,
                                .catalog = catalog,
                                .fs = fs,
                                .shell = shell,
                                .workers = workers,
                                .strand = strand,
                                .ops = ops,
                                .events = events,
                                .clock = clock,
                                .random = random,
                                .install = install}};
};

}  // namespace rb::builds::test
