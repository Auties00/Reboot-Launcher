#pragma once

#include <chrono>
#include <memory>
#include <string_view>

#include "reboot/builds/destination_suggestion.hpp"
#include "reboot/builds/install_request.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/operation.hpp"

namespace reboot {
class Executor;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IDiskInfo;
class IFileSystem;
}  // namespace reboot::ports

namespace reboot::net {
class ResumableDownloader;
}

namespace reboot::catalog {
class CatalogService;
}

namespace reboot::builds {

class ClTable;
class IArchiveExtractor;
class Library;

// <parent of dest>/.reboot-staging/<dest name>/ holds the archive, its resume sidecar and content/.
inline constexpr std::string_view kStagingDirName = ".reboot-staging";

// A volume query can hang on a network or empty optical drive; the op then ends TimedOut.
inline constexpr std::chrono::seconds kSuggestDestinationDeadline{30};

struct InstallerConfig {
    DestinationPolicy policy = DestinationPolicy::UnderDataRoot;
    // <data root>/builds, used by UnderDataRoot.
    NativePath data_root_builds_dir;
};

struct BuildInstallerDeps {
    const catalog::CatalogService& catalog;
    const ClTable& cl_table;
    Library& library;
    net::ResumableDownloader& downloader;
    IArchiveExtractor& extractor;
    ports::IDiskInfo& disk;
    ports::IFileSystem& fs;
    WorkerPool& workers;
    Executor& strand;
    OpRegistry& ops;
};

// Capabilities: game-builds.download, game-builds.download-lifecycle, game-builds.default-destination,
// game-builds.post-download-registration, game-builds.extract, game-builds.+1, game-builds.+4,
// game-builds.+45, game-builds.+59.
// Strand-only; disk, hashing, extraction and PE reads run on the WorkerPool. Phases follow InstallPhase.
// Extracting ends with one rename of content/ to the destination, so a build is all there or absent.
// Cancel and download failures keep the archive for resume; a bad archive and a partial content/ go.
// The catalog version stands in when detection from the files settles none.
class BuildInstaller {
public:
    BuildInstaller(BuildInstallerDeps deps, InstallerConfig config);
    ~BuildInstaller();
    BuildInstaller(const BuildInstaller&) = delete;
    BuildInstaller& operator=(const BuildInstaller&) = delete;

    // OpKind::Generic with kSuggestDestinationDeadline, completing with a DestinationSuggestion.
    Result<OpHandle> start_suggest_destination(std::string_view entry, DisconnectPolicy policy);

    // OpKind::Install, completing with an InstallOutcome. Validates the entry, name and root at once.
    // Single flight per destination: the same entry and name get the running op, else destination_busy.
    Result<OpHandle> start_install(InstallRequest request, DisconnectPolicy policy);

    // OpKind::Generic: drops the staged archive kept for `destination`; builds.destination_busy
    // while an install writes there.
    Result<OpHandle> start_discard_staging(const NativePath& destination, DisconnectPolicy policy);

    // OpKind::Generic with kRemoveDeadline: "Delete files" for an UnregisteredInstall of this engine
    // run; any other folder is builds.unknown_install_folder.
    Result<OpHandle> start_delete_unregistered(const NativePath& folder, DisconnectPolicy policy);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::builds
