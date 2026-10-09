#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/builds/build_layout.hpp"
#include "reboot/builds/detected_version.hpp"
#include "reboot/builds/installed_build.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/library_document.hpp"
#include "reboot/support/support_role.hpp"

namespace rb {
class EventBus;
class Executor;
class IClock;
class IRandom;
class WorkerPool;
struct InstallLayout;
}  // namespace rb

namespace rb::ports {
class IFileSystem;
class IShellLauncher;
}  // namespace rb::ports

namespace rb::catalog {
class CatalogService;
}

namespace rb::builds {

class ClTable;
class IBuildUsage;

enum class RemoveFiles : u8 { Keep, Trash, Delete };

// What a change does when the build is in use by a live session.
enum class RunningPolicy : u8 { Refuse, StopSessions };

// Absent members keep their value. A new version makes the source User.
struct BuildPatch {
    std::optional<std::string> name;
    std::optional<GameVersion> version;
    std::optional<Changelist> cl;
};

struct NewBuild {
    std::string name;
    NativePath root;
    std::optional<DetectedVersion> version;
    // Resolved at `root` by the caller, so the first launch skips the walk.
    std::optional<BuildLayout> layout;
    std::optional<CatalogEntryId> catalog_entry;
    bool needs_relocation = false;
};

// remove_tree reports no progress, so a removal gets a fixed bound instead of a liveness one.
inline constexpr std::chrono::minutes kRemoveDeadline{30};

struct LibraryDeps {
    storage::DocumentStore<storage::LibraryDocument>& store;
    IBuildUsage& usage;
    const ClTable& cl_table;
    const catalog::CatalogService& catalog;
    ports::IFileSystem& fs;
    ports::IShellLauncher& shell;
    WorkerPool& workers;
    Executor& strand;
    OpRegistry& ops;
    EventBus& events;
    const IClock& clock;
    IRandom& random;
    const InstallLayout& install;
};

// Capabilities: game-builds.library, game-builds.cli-store, game-builds.+91.
// Strand-only, over data/library.json, of which the engine is the only writer. Each edit is one
// store update and publishes LibraryChangedEvent. Entries keep the version source and the layout.
class Library {
public:
    explicit Library(LibraryDeps deps);
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    [[nodiscard]] std::vector<InstalledBuild> list() const;
    // builds.not_found for an unknown id.
    [[nodiscard]] Result<InstalledBuild> get(BuildId id) const;

    // By id, so a rename keeps it; an id no longer listed reads as unset.
    [[nodiscard]] std::optional<BuildId> selected(support::SupportRole role) const;
    // nullopt clears the role.
    Result<void> select(support::SupportRole role, std::optional<BuildId> id);

    Result<InstalledBuild> update(BuildId id, const BuildPatch& patch);

    // OpKind::Import, completing with the InstalledBuild. Resolves and detects at `root`: files that
    // settle another confirmed version are builds.version_mismatch, files that settle none keep it.
    Result<OpHandle> start_relocate(BuildId id, NativePath root, RunningPolicy running, DisconnectPolicy policy);

    // Trimmed, non-empty and unique without regard to ASCII case. `except` is a build being renamed.
    [[nodiscard]] Result<std::string> check_name(std::string_view name, std::optional<BuildId> except) const;
    // Lexical; import and install repeat it on canonical paths on the WorkerPool.
    [[nodiscard]] Result<void> check_root(const NativePath& root, std::optional<BuildId> except) const;

    // Re-checks the name and the root, then adds the entry in one store write.
    Result<InstalledBuild> add(NewBuild build);

    // OpKind::Generic with kRemoveDeadline. The entry goes only once the files were handled at the
    // root, so a failure leaves it listed with builds.remove_failed.
    Result<OpHandle> start_remove(BuildId id, RemoveFiles files, RunningPolicy running, DisconnectPolicy policy);

    // Exact version first, then the rest of its bucket, newest first; unconfirmed builds are left out.
    [[nodiscard]] std::vector<InstalledBuild> find_compatible(const GameVersion& version) const;
    // A version string from a server listing; one that does not parse matches nothing.
    [[nodiscard]] std::vector<InstalledBuild> find_compatible(std::string_view remote_version) const;

    // For play and host preflight: the stored layout if LayoutResolver::still_valid, else a new walk.
    void resolve_layout(BuildId id, CancelToken token, UniqueFunction<void(Result<BuildLayout>)> done);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::builds
