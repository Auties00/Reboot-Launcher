#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/publish/host_identity.hpp"
#include "reboot/publish/identity_hold.hpp"
#include "reboot/publish/identity_load_issue.hpp"

namespace reboot {
class Executor;
class IRandom;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
}

namespace reboot::publish {

// The file inside an export directory.
inline constexpr std::string_view kIdentityExportFile = "identity.json";

// Capabilities: hosting.+35, hosting.share.
// Strand-only; file work runs on the WorkerPool. Owns state/host-identity/<profile>.json, one
// owner-only file per host profile holding {server_id, token}.
// - A server id is minted here with uuid_v4.
// - Each token is registered with the Logger's Redactor when loaded or received.
// - Writes go through IFileSystem::atomic_replace inside the owner-only directory, then
//   restrict_to_owner; a failed write is kept in memory and retried with the next change or flush().
// - Rotation happens only through rotate(), which the publisher calls on an UNAUTHORIZED answer to
//   its own HostRegister and reports as a notice; nothing rotates silently.
class HostIdentityStore {
public:
    HostIdentityStore(ports::IFileSystem& fs, WorkerPool& workers, Executor& strand, IRandom& random, OpRegistry& ops,
                      NativePath dir);
    ~HostIdentityStore();
    HostIdentityStore(const HostIdentityStore&) = delete;
    HostIdentityStore& operator=(const HostIdentityStore&) = delete;

    // Blocking: engine startup only, before the strand runs. Reads the file of each host profile
    // (the port lists no directories). Fails with publish.identity_dir_unavailable when the
    // owner-only directory cannot be created; the engine then calls load_memory_only().
    // An unreadable file is moved aside and reported, never a failure.
    [[nodiscard]] Result<std::vector<IdentityLoadIssue>> load(std::span<const HostProfileId> profiles);
    // For a data root that could not be created: identities live in memory and tokens are lost at exit.
    void load_memory_only();

    // A copy, so it stays valid across rotate(), start_import() and remove().
    [[nodiscard]] std::optional<ServerId> server_id(const HostProfileId& profile) const;

    // Mints a fresh identity when the profile has none; an existing one is returned unchanged.
    Result<ServerId> ensure(const HostProfileId& profile);

    // ensure() and then hold. Fails with publish.profile_busy while another hold exists.
    Result<IdentityHold> acquire(const HostProfileId& profile);

    // Called at HostRegistered. Memory is updated at once; `saved` runs on the strand once the file
    // is written, or with the write error (then publish.token_not_saved is noticed).
    void save_token(const IdentityHold& hold, HostToken token, UniqueFunction<void(Result<void>)> saved);

    // A fresh server id with no token, after UNAUTHORIZED. Returns the new id at once.
    ServerId rotate(const IdentityHold& hold, UniqueFunction<void(Result<void>)> saved);

    // A deleted host profile. Fails at once with publish.identity_in_use while held; otherwise the
    // identity leaves memory at once and `removed` runs on the strand once the file is gone.
    Result<void> remove(const HostProfileId& profile, UniqueFunction<void(Result<void>)> removed);

    // An explicit, user-started move to another PC or VPS (OpKind::Generic). `destination` is a new
    // directory, created owner-only before kIdentityExportFile is written into it, so the token is
    // never readable by others, even on a shared folder. Fails at once with
    // publish.identity_not_registered when there is no token; the op fails with
    // publish.export_destination_exists when `destination` exists.
    Result<OpHandle> start_export(const HostProfileId& profile, NativePath destination, DisconnectPolicy policy);
    // Replaces the profile's identity with an exported one (OpKind::Import) and completes with its
    // ServerId. Fails at once with publish.identity_in_use while held; the op fails with
    // publish.identity_file_invalid when `source` is not an export directory with a token. The
    // identity is held until its file is written; a failed or cancelled import restores the old one.
    Result<OpHandle> start_import(const HostProfileId& profile, NativePath source, DisconnectPolicy policy);

    // ShutdownCoordinator's store step; `done` runs on the strand.
    void flush(UniqueFunction<void(Result<void>)> done);

private:
    friend class IdentityHold;
    void release(const HostProfileId& profile) noexcept;
    [[nodiscard]] const HostIdentity& held_identity(const HostProfileId& profile) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::publish
