#pragma once

#include <memory>
#include <optional>
#include <string>

#include "reboot/builds/layout_resolver.hpp"
#include "reboot/builds/pe_version_reader.hpp"
#include "reboot/builds/user_version.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"

namespace rb {
class Executor;
class UserRequestRegistry;
class WorkerPool;
}  // namespace rb

namespace rb::ports {
class IFileSystem;
}

namespace rb::catalog {
class CatalogService;
}

namespace rb::builds {

class ClTable;
class Library;

struct ImportRequest {
    NativePath path;
    std::string name;
    // The pick after NeedsShippingChoice, relative to `path`.
    std::optional<NativePath> shipping_exe;
    // Wins over detection, with source User; a differing detected version is logged.
    std::optional<UserVersion> version;
    // Set by "Import anyway" after an install: the build keeps that origin, and the entry's version
    // stands in, with source Catalog, when the files settle none.
    std::optional<CatalogEntryId> catalog_entry;
    // False completes with NeedsUserVersion instead of raising ChooseVersion.
    bool ask_user = true;
};

struct ImportServiceDeps {
    Library& library;
    const catalog::CatalogService& catalog;
    const ClTable& cl_table;
    ports::IFileSystem& fs;
    UserRequestRegistry& requests;
    WorkerPool& workers;
    Executor& strand;
    OpRegistry& ops;
};

// Capabilities: game-builds.import, game-builds.version-detection, game-builds.+91.
// Strand-only; never writes into the folder. Order: validate, resolve, detect, version cap, add.
class ImportService {
public:
    explicit ImportService(ImportServiceDeps deps);
    // Settles running imports as cancelled; their pending work never reaches this object.
    ~ImportService();
    ImportService(const ImportService&) = delete;
    ImportService& operator=(const ImportService&) = delete;

    // The folder's name, made unique with "-1", "-2", ...
    [[nodiscard]] std::string suggest_name(const NativePath& path) const;

    // OpKind::Import, completing with an ImportOutcome. A version above support::above_version_cap
    // is builds.unsupported_version; the support policy's runner and server rules apply at launch.
    Result<OpHandle> start_import(ImportRequest request, DisconnectPolicy policy);

private:
    struct Impl;

    ImportServiceDeps deps_;
    LayoutResolver resolver_;
    PeVersionReader reader_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::builds
