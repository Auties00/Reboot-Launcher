#include "reboot/components/component_store.hpp"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include "archive_extract.hpp"
#include "messages.hpp"
#include "reboot/components/component_changed_event.hpp"
#include "reboot/components/component_problem.hpp"
#include "reboot/components/deletion_guard.hpp"
#include "reboot/components/manifest_service.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/net/download_error.hpp"
#include "reboot/net/resumable_downloader.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/ports/os_services.hpp"
#include "store_index.hpp"

namespace rb::components {

namespace {

constexpr std::string_view kPhaseVerify = "verify";
constexpr std::string_view kPhaseDownload = "download";
constexpr std::string_view kPhaseExtract = "extract";

// Extraction reports progress at most this often, in archive bytes.
constexpr u64 kExtractProgressStep = 4u << 20;

[[nodiscard]] Diagnostic store_failed(const NativePath& path, std::optional<Diagnostic> cause = std::nullopt) {
    DiagBuilder builder = make_diag(ErrorDomain::Components, kStoreFailed).arg("path", path);
    if (cause) std::move(builder).cause(std::move(*cause));
    return std::move(builder).build();
}

[[nodiscard]] Diagnostic store_failed(const NativePath& path, const std::error_code& error) {
    return make_diag(ErrorDomain::Components, kStoreFailed)
        .arg("path", path)
        .os(SystemError{SystemError::Origin::Host, error.value()})
        .detail(error.message())
        .build();
}

[[nodiscard]] Diagnostic cancelled_fetch(const ComponentRef& ref) {
    return make_diag(ErrorDomain::Components, kDownloadFailed)
        .arg("component", ref.id)
        .arg("version", ref.version)
        .kind(ErrorKind::Cancelled)
        .build();
}

[[nodiscard]] bool asks_security_probe(HoldFailureKind kind) noexcept {
    return kind == HoldFailureKind::Missing || kind == HoldFailureKind::AccessDenied;
}

// Removes a file the store owns; absence is success.
void remove_file(const NativePath& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

}  // namespace

struct ComponentStore::Impl {
    struct PinRecord {
        ComponentRef ref;
        SessionId session;
    };

    struct ProblemRecord {
        ComponentProblem problem;
        u64 serial = 0;
    };

    struct Waiter {
        u64 id = 0;
        UniqueFunction<void(Result<void>)> done;
        ProgressSink progress;
        CancelRegistration registration;
    };

    // One fetch per component version; later callers join it.
    struct Fetch {
        StoreEntry target;
        // The version was stored and believed good when the fetch started.
        bool was_ready = false;
        bool started = false;
        CancelSource cancel;
        std::vector<Waiter> waiters;
        std::size_t next_file = 0;
        u64 done_bytes = 0;
        u64 total_bytes = 0;
        bool vanish_retried = false;
        // Callbacks of an earlier fetch of the same version find a different generation.
        u64 generation = 0;
    };

    struct RemoveJob {
        std::string component_id;
        Operation<void>* op = nullptr;
    };

    using Verified = std::optional<HoldFailure>;

    Impl(ComponentStoreDeps store_deps, const AppLayout& layout)
        : deps(store_deps),
          root(layout.components_dir()),
          staging(root / ".staging"),
          index_file(root / "index.json"),
          guard(store_deps.watcher, store_deps.strand, [this](const NativePath& file) { on_guard_report(file); }) {
        deps.manifest.add_listener([this](const ReleaseManifest&) { on_manifest_changed(); });
    }

    // Paths

    [[nodiscard]] NativePath payload_path(const PayloadFile& file) const {
        return root / "payload" / to_hex(file.file.sha256) / NativePath(std::string(payload_file_name(file.role)));
    }
    [[nodiscard]] NativePath runtime_root(const Sha256Digest& sha) const { return root / "runtime" / to_hex(sha); }
    // Per version, so two versions that share a file never download it into one part file, while a
    // fetch of the same version again resumes it. Ids come from the manifest, so they are hashed.
    [[nodiscard]] NativePath staged_path(const ComponentRef& ref, const Sha256Digest& sha) const {
        const std::string key = std::to_string(static_cast<int>(ref.kind)) + '\n' + ref.id + '\n' + ref.version;
        const std::string tag = to_hex(sha256(std::span(reinterpret_cast<const u8*>(key.data()), key.size())));
        return staging / (to_hex(sha) + "-" + tag.substr(0, 16));
    }
    [[nodiscard]] NativePath staged_tree(const ComponentRef& ref, const Sha256Digest& sha) const {
        NativePath out = staged_path(ref, sha);
        out += ".d";
        return out;
    }

    // Lookups

    [[nodiscard]] StoreEntry* find_entry(const ComponentRef& ref) {
        for (StoreEntry& entry : entries)
            if (entry.ref == ref) return &entry;
        return nullptr;
    }
    [[nodiscard]] const StoreEntry* find_entry(const ComponentRef& ref) const {
        for (const StoreEntry& entry : entries)
            if (entry.ref == ref) return &entry;
        return nullptr;
    }

    [[nodiscard]] u32 pin_count(const ComponentRef& ref) const {
        u32 count = 0;
        for (const auto& [id, pin] : pins)
            if (pin.ref == ref) ++count;
        return count;
    }

    [[nodiscard]] std::span<const PayloadRole> required_roles() const {
        return required_payload_roles(deps.manifest.platform().os);
    }

    [[nodiscard]] StoreEntry payload_target(const PayloadEntry& payload) const {
        StoreEntry entry;
        entry.ref = ComponentRef{ComponentKind::Payload, std::string(kPayloadComponentId), payload.version.to_string()};
        entry.payload_abi = payload.payload_abi;
        for (const PayloadRole role : required_roles()) {
            const PayloadFile* file = payload.find(role);
            if (file == nullptr) continue;
            entry.files.push_back(*file);
            entry.size_bytes += file->file.size;
        }
        return entry;
    }

    [[nodiscard]] static StoreEntry runtime_target(const RuntimeEntry& runtime) {
        StoreEntry entry;
        entry.ref = ComponentRef{ComponentKind::Runtime, runtime.id, runtime.version};
        entry.runtime_kind = runtime.kind;
        entry.archive = runtime.archive;
        return entry;
    }

    // The versions the manifest selects for new sessions.
    [[nodiscard]] std::vector<StoreEntry> selected_targets() const {
        std::vector<StoreEntry> out;
        if (auto payload = deps.manifest.payload()) out.push_back(payload_target(*payload));
        for (const RuntimeEntry& runtime : deps.manifest.runtimes()) out.push_back(runtime_target(runtime));
        return out;
    }

    [[nodiscard]] bool is_selected(const ComponentRef& ref) const {
        for (const StoreEntry& target : selected_targets())
            if (target.ref == ref) return true;
        return false;
    }

    [[nodiscard]] ComponentInfo info_for(const ComponentRef& ref) const {
        ComponentInfo info{.ref = ref};
        info.pins = pin_count(ref);
        info.selected = is_selected(ref);
        const bool fetching = fetches.contains(ref);
        if (const StoreEntry* entry = find_entry(ref)) {
            info.state = fetching ? ComponentState::Fetching
                         : entry->broken ? ComponentState::Broken
                                         : ComponentState::Ready;
            info.size_bytes = entry->size_bytes;
            info.last_good = entry->last_good;
            return info;
        }
        info.state = fetching ? ComponentState::Fetching : ComponentState::Missing;
        for (const StoreEntry& target : selected_targets())
            if (target.ref == ref) info.size_bytes = target.size_bytes != 0 ? target.size_bytes : target.archive.size;
        return info;
    }

    void publish_changed(const ComponentRef& ref) {
        ComponentChangedEvent event{.info = info_for(ref), .problem = std::nullopt};
        if (const auto found = problems.find(ref.id); found != problems.end()) event.problem = found->second.problem;
        deps.events.publish(EventKind::ComponentChanged, std::move(event), EventScope{.coalesce_key = ref.id});
    }

    // Problems

    void raise_problem(const ComponentRef& ref, const NativePath& file, std::optional<PayloadRole> role,
                       ComponentProblemKind kind, HoldFailureKind failure) {
        if (role == PayloadRole::Winhost) kind = ComponentProblemKind::HelperQuarantined;
        const u64 serial = ++problem_serial;
        problems[ref.id] = ProblemRecord{.problem = ComponentProblem{.kind = kind, .component = ref, .file = file},
                                         .serial = serial};
        if (StoreEntry* entry = find_entry(ref)) entry->broken = true;
        publish_changed(ref);
        if (!asks_security_probe(failure)) return;
        deps.workers.submit<std::optional<ports::SecurityProducts>>(
            [security = &deps.security](CancelToken) { return security->probe(); }, CancelToken{}, deps.strand,
            [this, id = ref.id, serial](Result<std::optional<ports::SecurityProducts>> probed) {
                const auto found = problems.find(id);
                if (found == problems.end() || found->second.serial != serial) return;
                // A failed probe leaves the problem unattributed.
                if (probed && *probed) {
                    found->second.problem.security = std::move(*probed);
                    found->second.problem.remediation = guided_remediation(found->second.problem.security);
                }
                publish_changed(found->second.problem.component);
            });
    }

    [[nodiscard]] bool refetching(const ComponentRef& ref) const {
        const auto found = problems.find(ref.id);
        return found != problems.end() && found->second.problem.component == ref &&
               found->second.problem.recovery == RecoveryState::Refetching;
    }

    void settle_problem(const ComponentRef& ref, const Result<void>& result) {
        if (!refetching(ref)) return;
        ComponentProblem& problem = problems[ref.id].problem;
        if (result) {
            problem.recovery = RecoveryState::Recovered;
        } else {
            problem.recovery = RecoveryState::RefetchFailed;
            problem.refetch_error = result.error();
        }
    }

    // Fetching

    void fetch(StoreEntry target, CancelToken token, ProgressSink progress, UniqueFunction<void(Result<void>)> done) {
        const ComponentRef ref = target.ref;
        auto found = fetches.find(ref);
        if (found == fetches.end()) {
            auto created = std::make_unique<Fetch>();
            const StoreEntry* stored = find_entry(ref);
            created->generation = next_generation++;
            created->target = std::move(target);
            if (stored != nullptr && created->target.ref.kind == ComponentKind::Runtime)
                created->target.size_bytes = stored->size_bytes;
            found = fetches.emplace(ref, std::move(created)).first;
            publish_changed(ref);
        }
        Fetch& f = *found->second;
        const u64 id = next_waiter++;
        f.waiters.push_back(Waiter{.id = id, .done = std::move(done), .progress = std::move(progress), .registration = {}});
        f.waiters.back().registration = token.on_cancel([this, ref, id](CancelReason) {
            deps.strand.post([this, ref, id] { drop_waiter(ref, id); });
        });
        // Before load the index is not read yet, and load clears staging.
        if (!f.started && !maintenance_running && loaded) start_fetch(f);
    }

    void drop_waiter(const ComponentRef& ref, u64 id) {
        const auto found = fetches.find(ref);
        if (found == fetches.end()) return;
        Fetch& f = *found->second;
        const auto waiter = std::ranges::find(f.waiters, id, &Waiter::id);
        if (waiter == f.waiters.end()) return;
        UniqueFunction<void(Result<void>)> done = std::move(waiter->done);
        f.waiters.erase(waiter);
        if (f.waiters.empty()) {
            if (f.started) f.cancel.cancel(CancelReason::User);
            else finish(f, std::unexpected(cancelled_fetch(ref)));
        }
        done(std::unexpected(cancelled_fetch(ref)));
    }

    void start_fetch(Fetch& f) {
        f.started = true;
        const StoreEntry* stored = find_entry(f.target.ref);
        f.was_ready = stored != nullptr && !stored->broken;
        if (f.target.ref.kind == ComponentKind::Payload) {
            f.total_bytes = f.target.size_bytes;
            return next_payload_file(f);
        }
        check_runtime(f);
    }

    void report(Fetch& f, Progress progress) {
        for (Waiter& waiter : f.waiters)
            if (waiter.progress) waiter.progress(progress);
    }

    [[nodiscard]] bool stop_if_cancelled(Fetch& f) {
        if (!f.cancel.cancelled()) return false;
        finish(f, std::unexpected(cancelled_fetch(f.target.ref)));
        return true;
    }

    void next_payload_file(Fetch& f) {
        if (f.next_file == f.target.files.size()) return finish(f, {});
        const PayloadFile file = f.target.files[f.next_file];
        const NativePath path = payload_path(file);
        const ComponentRef ref = f.target.ref;
        const u64 gen = f.generation;
        deps.workers.submit<Verified>(
            [fs = &deps.fs, path, sha = file.file.sha256](CancelToken) -> Result<Verified> {
                auto held = IntegrityHold::acquire(*fs, path, sha);
                if (held) return Verified{};
                return Verified{std::move(held.error())};
            },
            f.cancel.token(), deps.strand, [this, ref, gen, file, path](Result<Verified> verified) {
                Fetch* live = live_fetch(ref, gen);
                if (live == nullptr || stop_if_cancelled(*live)) return;
                if (!verified) return finish(*live, std::unexpected(std::move(verified.error())));
                // A file another process holds is not blamed; the session's own hold checks it again.
                if (!*verified || (*verified)->kind == HoldFailureKind::InUse) {
                    live->done_bytes += file.file.size;
                    report(*live, Progress{.phase = kPhaseVerify, .done = live->done_bytes, .total = live->total_bytes});
                    ++live->next_file;
                    return next_payload_file(*live);
                }
                if (live->was_ready && !refetching(ref))
                    raise_problem(ref, path, file.role, ComponentProblemKind::FileVanishedAfterVerify, (*verified)->kind);
                download(*live, file.file, staged_path(ref, file.file.sha256), 0, std::nullopt, file.role,
                         [this, file, path](Fetch& owner, NativePath staged) {
                             store_payload_file(owner, std::move(staged), file, path);
                         });
            });
    }

    // Tries each url in order; `stored` runs once a staged copy matched its sha256.
    using OnStaged = UniqueFunction<void(Fetch&, NativePath staged)>;

    void download(Fetch& f, const RemoteFile& remote, NativePath staged, std::size_t url_index,
                  std::optional<Diagnostic> last_error, std::optional<PayloadRole> role, OnStaged on_staged) {
        const ComponentRef ref = f.target.ref;
        if (url_index >= remote.urls.size()) {
            DiagBuilder builder = make_diag(ErrorDomain::Components, kDownloadFailed)
                                      .arg("component", ref.id)
                                      .arg("version", ref.version)
                                      .retryable();
            if (last_error) std::move(builder).cause(std::move(*last_error));
            return finish(f, std::unexpected(std::move(builder).build()));
        }
        net::DownloadRequest request{.url = remote.urls[url_index], .file = staged, .expected_size = remote.size};
        const u64 base = f.done_bytes;
        const u64 gen = f.generation;
        // Shared by both callbacks, so a synchronous refusal and a later failure take the same path.
        auto on_done = std::make_shared<UniqueFunction<void(Result<net::DownloadResult>)>>(
            [this, ref, gen, remote, staged, url_index, role, on_staged = std::move(on_staged)](
                Result<net::DownloadResult> result) mutable {
                Fetch* live = live_fetch(ref, gen);
                if (live == nullptr || stop_if_cancelled(*live)) return;
                if (!result)
                    return download(*live, remote, std::move(staged), url_index + 1, std::move(result.error()), role,
                                    std::move(on_staged));
                verify_staged(*live, remote, std::move(staged), url_index, role, std::move(on_staged));
            });
        auto started = deps.downloader.start(
            std::move(request), f.cancel.token(),
            [this, ref, gen, base](const net::DownloadProgress& downloaded) {
                Fetch* live = live_fetch(ref, gen);
                if (live == nullptr) return;
                report(*live, Progress{.phase = kPhaseDownload,
                                       .done = base + downloaded.done,
                                       .total = live->total_bytes,
                                       .rate_per_s = downloaded.bytes_per_s});
            },
            [on_done](Result<net::DownloadResult> result) { (*on_done)(std::move(result)); });
        if (!started) (*on_done)(std::unexpected(std::move(started.error())));
    }

    void verify_staged(Fetch& f, const RemoteFile& remote, NativePath staged, std::size_t url_index,
                       std::optional<PayloadRole> role, OnStaged on_staged) {
        const ComponentRef ref = f.target.ref;
        const u64 gen = f.generation;
        deps.workers.submit<Verified>(
            [fs = &deps.fs, staged, sha = remote.sha256](CancelToken) -> Result<Verified> {
                auto held = IntegrityHold::acquire(*fs, staged, sha);
                if (held) return Verified{};
                return Verified{std::move(held.error())};
            },
            f.cancel.token(), deps.strand,
            [this, ref, gen, remote, staged, url_index, role, on_staged = std::move(on_staged)](
                Result<Verified> verified) mutable {
                Fetch* live = live_fetch(ref, gen);
                if (live == nullptr || stop_if_cancelled(*live)) return;
                if (!verified) return finish(*live, std::unexpected(std::move(verified.error())));
                if (!*verified) return on_staged(*live, std::move(staged));

                const HoldFailure failure = std::move(**verified);
                if (failure.kind == HoldFailureKind::Mismatch) {
                    remove_file(staged);
                    remove_file(NativePath(staged) += net::kResumeSidecarSuffix);
                    Diagnostic mismatch =
                        make_diag(ErrorDomain::Components, kChecksumMismatch).arg("file", remote.urls[url_index]).build();
                    return download(*live, remote, std::move(staged), url_index + 1, std::move(mismatch), role,
                                    std::move(on_staged));
                }
                // The staged file vanished or became unreadable between the download and its hash.
                if (!live->vanish_retried) {
                    live->vanish_retried = true;
                    raise_problem(ref, staged, role, ComponentProblemKind::VanishedAfterDownload, failure.kind);
                    remove_file(NativePath(staged) += net::kResumeSidecarSuffix);
                    return download(*live, remote, std::move(staged), url_index, std::nullopt, role, std::move(on_staged));
                }
                ComponentProblem problem;
                if (const auto found = problems.find(ref.id); found != problems.end()) problem = found->second.problem;
                problem.kind = role == PayloadRole::Winhost ? ComponentProblemKind::HelperQuarantined
                                                            : ComponentProblemKind::VanishedAfterDownload;
                problem.file = staged;
                problem.refetch_error = to_diagnostic(failure);
                finish(*live, std::unexpected(to_diagnostic(problem)));
            });
    }

    void store_payload_file(Fetch& f, NativePath staged, const PayloadFile& file, const NativePath& final_path) {
        const ComponentRef ref = f.target.ref;
        const u64 gen = f.generation;
        deps.workers.submit<void>(
            [fs = &deps.fs, staged, final_path](CancelToken) -> Result<void> {
                if (auto created = fs->create_dirs_owner_only(final_path.parent_path()); !created)
                    return std::unexpected(store_failed(final_path.parent_path(), std::move(created.error())));
                std::error_code error;
                std::filesystem::rename(staged, final_path, error);
                if (error) return std::unexpected(store_failed(final_path, error));
                return {};
            },
            CancelToken{}, deps.strand, [this, ref, gen, size = file.file.size](Result<void> stored) {
                Fetch* live = live_fetch(ref, gen);
                if (live == nullptr) return;
                if (!stored) return finish(*live, std::move(stored));
                live->done_bytes += size;
                ++live->next_file;
                next_payload_file(*live);
            });
    }

    void check_runtime(Fetch& f) {
        const ComponentRef ref = f.target.ref;
        const u64 gen = f.generation;
        const StoreEntry* stored = find_entry(ref);
        if (stored == nullptr || stored->broken) return download_runtime(f);
        deps.workers.submit<Verified>(
            [fs = &deps.fs, path = runtime_root(f.target.archive.sha256)](CancelToken) -> Result<Verified> {
                auto revision = fs->revision(path);
                if (revision) return Verified{};
                return Verified{HoldFailure{classify_hold_failure(revision.error()), path, std::move(revision.error())}};
            },
            f.cancel.token(), deps.strand, [this, ref, gen](Result<Verified> verified) {
                Fetch* live = live_fetch(ref, gen);
                if (live == nullptr || stop_if_cancelled(*live)) return;
                if (!verified) return finish(*live, std::unexpected(std::move(verified.error())));
                if (!*verified) return finish(*live, {});
                if (live->was_ready && !refetching(ref))
                    raise_problem(ref, (*verified)->file, std::nullopt, ComponentProblemKind::FileVanishedAfterVerify,
                                  (*verified)->kind);
                download_runtime(*live);
            });
    }

    void download_runtime(Fetch& f) {
        f.total_bytes = f.target.archive.size;
        const RemoteFile archive = f.target.archive;
        download(f, archive, staged_path(f.target.ref, archive.sha256), 0, std::nullopt, std::nullopt,
                 [this](Fetch& owner, NativePath staged) { unpack_runtime(owner, std::move(staged)); });
    }

    void unpack_runtime(Fetch& f, NativePath staged) {
        const ComponentRef ref = f.target.ref;
        const Sha256Digest sha = f.target.archive.sha256;
        const NativePath tree = staged_tree(ref, sha);
        const NativePath final_root = runtime_root(sha);
        const u64 archive_size = f.target.archive.size;
        const u64 gen = f.generation;
        report(f, Progress{.phase = kPhaseExtract, .done = 0, .total = archive_size});
        deps.workers.submit<u64>(
            [this, ref, gen, staged, tree, final_root, archive_size](CancelToken token) -> Result<u64> {
                const auto extract_failed = [&](std::string detail) {
                    return make_diag(ErrorDomain::Components, kExtractFailed)
                        .arg("component", ref.id)
                        .arg("version", ref.version)
                        .detail(std::move(detail))
                        .fail();
                };
                if (auto cleared = deps.fs.remove_tree(tree); !cleared && cleared.error().kind != ErrorKind::NotFound)
                    return std::unexpected(store_failed(tree, std::move(cleared.error())));
                if (auto created = deps.fs.create_dirs_owner_only(tree); !created)
                    return std::unexpected(store_failed(tree, std::move(created.error())));
                u64 reported = 0;
                auto unpacked = extract_archive(staged, tree, token, [&](u64 read) {
                    if (read < reported + kExtractProgressStep) return;
                    reported = read;
                    deps.strand.post([this, ref, gen, read, archive_size] {
                        if (Fetch* live = live_fetch(ref, gen))
                            report(*live, Progress{.phase = kPhaseExtract, .done = read, .total = archive_size});
                    });
                });
                if (!unpacked) {
                    if (unpacked.error().cancelled) return std::unexpected(cancelled_fetch(ref));
                    return extract_failed(std::move(unpacked.error().detail));
                }
                if (auto created = deps.fs.create_dirs_owner_only(final_root.parent_path()); !created)
                    return std::unexpected(store_failed(final_root.parent_path(), std::move(created.error())));
                // A broken copy is replaced whole.
                if (auto cleared = deps.fs.remove_tree(final_root); !cleared && cleared.error().kind != ErrorKind::NotFound)
                    return std::unexpected(store_failed(final_root, std::move(cleared.error())));
                std::error_code error;
                std::filesystem::rename(tree, final_root, error);
                if (error) return std::unexpected(store_failed(final_root, error));
                remove_file(staged);
                return *unpacked;
            },
            f.cancel.token(), deps.strand, [this, ref, gen](Result<u64> unpacked) {
                Fetch* live = live_fetch(ref, gen);
                if (live == nullptr) return;
                if (!unpacked) return finish(*live, std::unexpected(std::move(unpacked.error())));
                live->target.size_bytes = *unpacked;
                finish(*live, {});
            });
    }

    [[nodiscard]] Fetch* live_fetch(const ComponentRef& ref, u64 generation) {
        const auto found = fetches.find(ref);
        if (found == fetches.end() || found->second->generation != generation) return nullptr;
        return found->second.get();
    }

    void finish(Fetch& f, Result<void> result) {
        const ComponentRef ref = f.target.ref;
        const auto found = fetches.find(ref);
        std::unique_ptr<Fetch> owned = std::move(found->second);
        fetches.erase(found);

        // Callers that joined after every earlier one gave up get a fresh fetch.
        if (!result && owned->cancel.cancelled() && !owned->waiters.empty()) {
            auto restarted = std::make_unique<Fetch>();
            restarted->target = std::move(owned->target);
            restarted->generation = next_generation++;
            restarted->waiters = std::move(owned->waiters);
            Fetch& again = *fetches.emplace(ref, std::move(restarted)).first->second;
            if (!maintenance_running) start_fetch(again);
            return;
        }

        StoreEntry* stored = find_entry(ref);
        if (result) {
            if (stored != nullptr) {
                untrack(*stored);
                const bool last_good = stored->last_good;
                *stored = owned->target;
                stored->last_good = last_good;
            } else {
                entries.push_back(owned->target);
                stored = &entries.back();
            }
            stored->broken = false;
            track(*stored);
            write_index();
        } else if (stored != nullptr && result.error().kind != ErrorKind::Cancelled) {
            stored->broken = true;
        }
        settle_problem(ref, result);
        publish_changed(ref);

        for (Waiter& waiter : owned->waiters) {
            waiter.registration.reset();
            waiter.done(result);
        }
        if (result) request_maintenance();
        if (fetches.empty()) post_maintenance();
    }

    // The deletion guard

    void track(const StoreEntry& entry) {
        for (const NativePath& path : tracked_paths(entry))
            if (auto tracked = guard.track(path); !tracked)
                REBOOT_LOG_WARN(Update, "Cannot watch {}: {}", display_utf8(path), tracked.error().id);
    }

    void untrack(const StoreEntry& entry) {
        for (const NativePath& path : tracked_paths(entry)) guard.untrack(path);
    }

    [[nodiscard]] std::vector<NativePath> tracked_paths(const StoreEntry& entry) const {
        std::vector<NativePath> out;
        if (entry.ref.kind == ComponentKind::Runtime) {
            out.push_back(runtime_root(entry.archive.sha256));
            return out;
        }
        for (const PayloadFile& file : entry.files) out.push_back(payload_path(file));
        return out;
    }

    void on_guard_report(const NativePath& file) {
        for (const StoreEntry& entry : entries) {
            if (entry.broken) continue;
            if (entry.ref.kind == ComponentKind::Runtime) {
                if (runtime_root(entry.archive.sha256).lexically_normal() == file)
                    return reverify(entry.ref, file, std::nullopt, std::nullopt);
                continue;
            }
            for (const PayloadFile& stored : entry.files)
                if (payload_path(stored).lexically_normal() == file)
                    return reverify(entry.ref, file, stored.role, stored.file.sha256);
        }
    }

    // A report is a suspect only: the file is checked before anything is raised.
    void reverify(const ComponentRef& ref, const NativePath& file, std::optional<PayloadRole> role,
                  std::optional<Sha256Digest> sha) {
        if (fetches.contains(ref) || !reverifying.insert(file).second) return;
        deps.workers.submit<Verified>(
            [fs = &deps.fs, file, sha](CancelToken) -> Result<Verified> {
                if (sha) {
                    auto held = IntegrityHold::acquire(*fs, file, *sha);
                    if (held) return Verified{};
                    return Verified{std::move(held.error())};
                }
                auto revision = fs->revision(file);
                if (revision) return Verified{};
                return Verified{HoldFailure{classify_hold_failure(revision.error()), file, std::move(revision.error())}};
            },
            CancelToken{}, deps.strand, [this, ref, file, role](Result<Verified> verified) {
                reverifying.erase(file);
                if (!verified || !*verified || (*verified)->kind == HoldFailureKind::InUse) return;
                const StoreEntry* entry = find_entry(ref);
                if (entry == nullptr || fetches.contains(ref)) return;
                raise_problem(ref, file, role, ComponentProblemKind::FileVanishedAfterVerify, (*verified)->kind);
                fetch(*entry, CancelToken{}, nullptr, [](Result<void>) {});
            });
    }

    // Garbage collection and removal

    void request_maintenance() {
        gc_requested = true;
        post_maintenance();
    }

    void post_maintenance() {
        if (maintenance_posted) return;
        maintenance_posted = true;
        deps.strand.post([this] {
            maintenance_posted = false;
            run_maintenance();
        });
    }

    [[nodiscard]] bool collectable(const StoreEntry& entry) const {
        return pin_count(entry.ref) == 0 && !entry.last_good && !is_selected(entry.ref);
    }

    void run_maintenance() {
        if (!loaded || maintenance_running || !fetches.empty()) return;
        std::vector<ComponentRef> targets;
        std::optional<RemoveJob> job;
        if (!remove_jobs.empty()) {
            job = remove_jobs.front();
            remove_jobs.pop_front();
            if (index_read_only) {
                complete_remove(*job, std::unexpected(read_only_error()));
                return post_maintenance_if_pending();
            }
            for (const StoreEntry& entry : entries)
                if (entry.ref.id == job->component_id && pin_count(entry.ref) == 0) targets.push_back(entry.ref);
        } else if (gc_requested) {
            gc_requested = false;
            // Directories a newer index names stay.
            if (index_read_only) return;
            for (const StoreEntry& entry : entries)
                if (collectable(entry)) targets.push_back(entry.ref);
        } else {
            return;
        }
        if (targets.empty()) {
            if (job) complete_remove(*job, remove_outcome(*job));
            return post_maintenance_if_pending();
        }

        std::vector<StoreEntry> removed;
        for (const ComponentRef& ref : targets) {
            const auto found = std::ranges::find(entries, ref, &StoreEntry::ref);
            untrack(*found);
            removed.push_back(std::move(*found));
            entries.erase(found);
        }
        // Content-addressed directories shared with a kept version stay.
        std::set<NativePath> dirs;
        for (const StoreEntry& entry : removed) {
            if (entry.ref.kind == ComponentKind::Runtime) {
                dirs.insert(runtime_root(entry.archive.sha256));
                continue;
            }
            for (const PayloadFile& file : entry.files) dirs.insert(payload_path(file).parent_path());
        }
        for (const StoreEntry& entry : entries) {
            if (entry.ref.kind == ComponentKind::Runtime) dirs.erase(runtime_root(entry.archive.sha256));
            for (const PayloadFile& file : entry.files) dirs.erase(payload_path(file).parent_path());
        }
        // Untracking a removed version also dropped the files it shared with a kept one.
        for (const StoreEntry& entry : entries)
            if (!entry.broken) track(entry);
        for (const StoreEntry& entry : removed) publish_changed(entry.ref);
        write_index();

        maintenance_running = true;
        deps.workers.submit<void>(
            [fs = &deps.fs, dirs = std::move(dirs)](CancelToken) -> Result<void> {
                std::optional<Diagnostic> first;
                for (const NativePath& dir : dirs) {
                    auto removed_dir = fs->remove_tree(dir);
                    if (!removed_dir && removed_dir.error().kind != ErrorKind::NotFound && !first)
                        first = store_failed(dir, std::move(removed_dir.error()));
                }
                if (first) return std::unexpected(std::move(*first));
                return {};
            },
            CancelToken{}, deps.strand, [this, job](Result<void> removed_dirs) {
                maintenance_running = false;
                if (!removed_dirs)
                    REBOOT_LOG_WARN(Update, "Component cleanup left files behind: {}", removed_dirs.error().id);
                if (job) complete_remove(*job, removed_dirs ? remove_outcome(*job) : std::move(removed_dirs));
                start_deferred_fetches();
                post_maintenance_if_pending();
            });
    }

    void post_maintenance_if_pending() {
        if (gc_requested || !remove_jobs.empty()) post_maintenance();
    }

    void start_deferred_fetches() {
        std::vector<ComponentRef> waiting;
        for (const auto& [ref, f] : fetches)
            if (!f->started) waiting.push_back(ref);
        for (const ComponentRef& ref : waiting)
            if (const auto found = fetches.find(ref); found != fetches.end() && !found->second->started)
                start_fetch(*found->second);
    }

    // components.pinned for a version a session pinned since the removal was accepted.
    [[nodiscard]] Result<void> remove_outcome(const RemoveJob& job) const {
        for (const StoreEntry& entry : entries)
            if (entry.ref.id == job.component_id)
                return make_diag(ErrorDomain::Components, kPinned)
                    .arg("component", entry.ref.id)
                    .arg("version", entry.ref.version)
                    .kind(ErrorKind::Conflict)
                    .fail();
        return {};
    }

    static void complete_remove(const RemoveJob& job, Result<void> outcome) {
        if (outcome) job.op->complete(Completed<void>{});
        else job.op->complete(Failed{std::move(outcome.error())});
    }

    // index.json

    [[nodiscard]] Diagnostic read_only_error() const {
        return make_diag(ErrorDomain::Components, kStoreFailed)
            .arg("path", index_file)
            .kind(ErrorKind::Unsupported)
            .detail("index.json has a newer schema")
            .build();
    }

    void write_index() {
        if (index_read_only) return;
        if (index_writing) {
            index_dirty = true;
            return;
        }
        index_writing = true;
        deps.workers.submit<void>(
            [fs = &deps.fs, file = index_file, bytes = serialize_store_index(entries)](CancelToken) -> Result<void> {
                if (auto created = fs->create_dirs_owner_only(file.parent_path()); !created) return created;
                return fs->atomic_replace(file, bytes, false);
            },
            CancelToken{}, deps.strand, [this](Result<void> written) {
                index_writing = false;
                // The next change writes the whole index again.
                if (!written) REBOOT_LOG_WARN(Update, "Cannot write the component index: {}", written.error().id);
                if (std::exchange(index_dirty, false)) write_index();
            });
    }

    void on_manifest_changed() {
        for (const StoreEntry& target : selected_targets()) publish_changed(target.ref);
        for (const StoreEntry& entry : entries) publish_changed(entry.ref);
        request_maintenance();
    }

    // Pins

    // ComponentStore wraps the id in a ComponentPin.
    [[nodiscard]] u64 add_pin(const ComponentRef& ref, SessionId session) {
        const u64 id = next_pin++;
        pins.emplace(id, PinRecord{ref, session});
        publish_changed(ref);
        return id;
    }

    struct HoldAttempt {
        PayloadSet payload;
        PayloadRole role{};
        CancelToken token;
        ProgressSink progress;
        UniqueFunction<void(Result<IntegrityHold>)> done;
        bool retried = false;
    };

    // On a missing, unreadable or changed file: report, re-fetch, then one more try.
    void hold_once(std::shared_ptr<HoldAttempt> attempt) {
        const StoredFile* stored = attempt->payload.find(attempt->role);
        using Held = std::expected<IntegrityHold, HoldFailure>;
        deps.workers.submit<Held>(
            [fs = &deps.fs, path = stored->path, sha = stored->sha256](CancelToken) -> Result<Held> {
                return IntegrityHold::acquire(*fs, path, sha);
            },
            attempt->token, deps.strand, [this, attempt](Result<Held> held) {
                if (!held) return attempt->done(std::unexpected(std::move(held.error())));
                if (*held) return attempt->done(std::move(**held));
                HoldFailure failure = std::move(held->error());
                const StoreEntry* entry = find_entry(attempt->payload.ref);
                if (failure.kind == HoldFailureKind::InUse || attempt->retried || attempt->token.cancelled() ||
                    entry == nullptr)
                    return attempt->done(std::unexpected(to_diagnostic(failure)));

                attempt->retried = true;
                raise_problem(attempt->payload.ref, failure.file, attempt->role,
                              ComponentProblemKind::FileVanishedAfterVerify, failure.kind);
                ProgressSink forward = [attempt](const Progress& step) {
                    if (attempt->progress) attempt->progress(step);
                };
                fetch(*entry, attempt->token, std::move(forward),
                      [this, attempt, failure = std::move(failure)](Result<void> refetched) {
                          if (!refetched) {
                              Diagnostic error = to_diagnostic(failure);
                              error.causes.push_back(std::move(refetched.error()));
                              return attempt->done(std::unexpected(std::move(error)));
                          }
                          hold_once(attempt);
                      });
            });
    }

    ComponentStoreDeps deps;
    NativePath root;
    NativePath staging;
    NativePath index_file;
    std::vector<StoreEntry> entries;
    bool loaded = false;
    // index.json has a newer schema: nothing is collected, removed or written back.
    bool index_read_only = false;
    DeletionGuard guard;
    std::map<u64, PinRecord> pins;
    u64 next_pin = 1;
    std::map<std::string, ProblemRecord, std::less<>> problems;
    u64 problem_serial = 0;
    std::map<ComponentRef, std::unique_ptr<Fetch>> fetches;
    u64 next_waiter = 1;
    u64 next_generation = 1;
    std::set<NativePath> reverifying;
    bool maintenance_running = false;
    bool maintenance_posted = false;
    bool gc_requested = false;
    std::deque<RemoveJob> remove_jobs;
    bool index_writing = false;
    bool index_dirty = false;
};

ComponentStore::ComponentStore(ComponentStoreDeps deps, const AppLayout& layout)
    : impl_(std::make_unique<Impl>(deps, layout)) {}

ComponentStore::~ComponentStore() = default;

void ComponentStore::load(UniqueFunction<void(Result<void>)> done) {
    struct Loaded {
        std::vector<StoreEntry> entries;
        std::optional<Diagnostic> index_problem;
        bool read_only = false;
    };
    Impl* impl = impl_.get();
    impl->deps.workers.submit<Loaded>(
        [impl](CancelToken) -> Result<Loaded> {
            ports::IFileSystem& fs = impl->deps.fs;
            if (auto created = fs.create_dirs_owner_only(impl->root); !created)
                return std::unexpected(store_failed(impl->root, std::move(created.error())));
            if (auto cleared = fs.remove_tree(impl->staging); !cleared && cleared.error().kind != ErrorKind::NotFound)
                return std::unexpected(store_failed(impl->staging, std::move(cleared.error())));
            if (auto created = fs.create_dirs_owner_only(impl->staging); !created)
                return std::unexpected(store_failed(impl->staging, std::move(created.error())));

            Loaded loaded;
            if (auto bytes = fs.read_all(impl->index_file)) {
                auto parsed = parse_store_index(*bytes);
                if (parsed) loaded.entries = std::move(*parsed);
                else loaded.index_problem = std::move(parsed.error());
                loaded.read_only = !parsed && parsed.error().kind == ErrorKind::Unsupported;
            } else if (bytes.error().kind != ErrorKind::NotFound) {
                loaded.index_problem = std::move(bytes.error());
            }

            for (StoreEntry& entry : loaded.entries) {
                if (entry.ref.kind == ComponentKind::Runtime) {
                    entry.broken = !fs.revision(impl->runtime_root(entry.archive.sha256)).has_value();
                    continue;
                }
                for (const PayloadFile& file : entry.files) {
                    auto held = IntegrityHold::acquire(fs, impl->payload_path(file), file.file.sha256);
                    if (!held && held.error().kind != HoldFailureKind::InUse) entry.broken = true;
                }
            }

            if (loaded.read_only) return loaded;
            // Version directories the index does not name are leftovers of an interrupted removal.
            std::set<NativePath> known;
            for (const StoreEntry& entry : loaded.entries) {
                if (entry.ref.kind == ComponentKind::Runtime) known.insert(impl->runtime_root(entry.archive.sha256));
                for (const PayloadFile& file : entry.files) known.insert(impl->payload_path(file).parent_path());
            }
            for (const NativePath& kind_dir : {impl->root / "payload", impl->root / "runtime"}) {
                std::error_code error;
                std::filesystem::directory_iterator it(kind_dir, error);
                std::vector<NativePath> orphans;
                for (; !error && it != std::filesystem::directory_iterator(); it.increment(error))
                    if (!known.contains(it->path())) orphans.push_back(it->path());
                for (const NativePath& orphan : orphans) (void)fs.remove_tree(orphan);
            }
            return loaded;
        },
        CancelToken{}, impl->deps.strand, [impl, done = std::move(done)](Result<Loaded> loaded) mutable {
            if (!loaded) return done(std::unexpected(std::move(loaded.error())));
            if (loaded->read_only)
                REBOOT_LOG_WARN(Update, "The component index has a newer schema; the store runs read-only");
            else if (loaded->index_problem)
                REBOOT_LOG_WARN(Update, "The component index is unreadable ({}); starting empty",
                                loaded->index_problem->id);
            impl->entries = std::move(loaded->entries);
            impl->index_read_only = loaded->read_only;
            impl->loaded = true;
            for (const StoreEntry& entry : impl->entries)
                if (!entry.broken) impl->track(entry);
            impl->start_deferred_fetches();
            impl->request_maintenance();
            done({});
        });
}

std::vector<ComponentInfo> ComponentStore::list() const {
    std::vector<ComponentInfo> out;
    for (const StoreEntry& entry : impl_->entries) out.push_back(impl_->info_for(entry.ref));
    for (const StoreEntry& target : impl_->selected_targets())
        if (impl_->find_entry(target.ref) == nullptr) out.push_back(impl_->info_for(target.ref));
    for (const auto& [ref, fetch] : impl_->fetches)
        if (impl_->find_entry(ref) == nullptr && !impl_->is_selected(ref)) out.push_back(impl_->info_for(ref));
    return out;
}

Result<OpHandle> ComponentStore::start_ensure(std::string_view component_id, DisconnectPolicy policy) {
    StoreEntry target;
    if (component_id == kPayloadComponentId) {
        auto payload = impl_->deps.manifest.payload();
        if (!payload) return std::unexpected(std::move(payload.error()));
        target = impl_->payload_target(*payload);
    } else {
        auto runtime = impl_->deps.manifest.runtime(component_id);
        if (!runtime) return std::unexpected(std::move(runtime.error()));
        target = Impl::runtime_target(*runtime);
    }
    auto [handle, op] = impl_->deps.ops.create<ComponentRef>(OpKind::ComponentEnsure, policy, std::nullopt);
    Operation<ComponentRef>* operation = &op;
    const ComponentRef ref = target.ref;
    impl_->fetch(
        std::move(target), op.token(), [operation](const Progress& progress) { operation->progress(progress); },
        [operation, ref](Result<void> fetched) {
            if (fetched) operation->complete(Completed<ComponentRef>{ref});
            else operation->complete(Failed{std::move(fetched.error())});
        });
    return handle;
}

Result<OpHandle> ComponentStore::start_remove(std::string_view component_id, DisconnectPolicy policy) {
    if (impl_->index_read_only) return std::unexpected(impl_->read_only_error());
    bool known = false;
    for (const StoreEntry& entry : impl_->entries) {
        if (entry.ref.id != component_id) continue;
        known = true;
        if (impl_->pin_count(entry.ref) != 0)
            return make_diag(ErrorDomain::Components, kPinned)
                .arg("component", entry.ref.id)
                .arg("version", entry.ref.version)
                .kind(ErrorKind::Conflict)
                .fail();
    }
    if (!known)
        for (const StoreEntry& target : impl_->selected_targets()) known = known || target.ref.id == component_id;
    if (!known)
        return make_diag(ErrorDomain::Components, kUnknownComponent)
            .arg("component", component_id)
            .kind(ErrorKind::NotFound)
            .fail();
    auto [handle, op] = impl_->deps.ops.create<void>(OpKind::Generic, policy, std::nullopt);
    impl_->remove_jobs.push_back(Impl::RemoveJob{.component_id = std::string(component_id), .op = &op});
    impl_->post_maintenance();
    return handle;
}

void ComponentStore::acquire_payload(SessionId session, CancelToken token, ProgressSink progress,
                                     UniqueFunction<void(Result<PinnedPayload>)> done) {
    auto payload = impl_->deps.manifest.payload();
    if (!payload) return done(std::unexpected(std::move(payload.error())));
    StoreEntry target = impl_->payload_target(*payload);
    const ComponentRef ref = target.ref;
    impl_->fetch(std::move(target), std::move(token), std::move(progress),
                 [this, impl = impl_.get(), ref, session, done = std::move(done)](Result<void> fetched) mutable {
                     if (!fetched) return done(std::unexpected(std::move(fetched.error())));
                     const StoreEntry* entry = impl->find_entry(ref);
                     if (entry == nullptr) return done(std::unexpected(internal_bug("components: fetched payload not stored")));
                     PayloadSet set{.ref = ref, .payload_abi = entry->payload_abi, .files = {}};
                     for (const PayloadFile& file : entry->files)
                         set.files.push_back(StoredFile{.role = file.role,
                                                        .path = impl->payload_path(file),
                                                        .sha256 = file.file.sha256,
                                                        .size = file.file.size});
                     done(PinnedPayload{.set = std::move(set),
                                        .pin = ComponentPin(*this, impl->add_pin(ref, session), ref, session)});
                 });
}

void ComponentStore::acquire_runtime(SessionId session, std::string_view runtime_id, CancelToken token,
                                     ProgressSink progress, UniqueFunction<void(Result<PinnedRuntime>)> done) {
    auto runtime = impl_->deps.manifest.runtime(runtime_id);
    if (!runtime) return done(std::unexpected(std::move(runtime.error())));
    StoreEntry target = Impl::runtime_target(*runtime);
    const ComponentRef ref = target.ref;
    impl_->fetch(std::move(target), std::move(token), std::move(progress),
                 [this, impl = impl_.get(), ref, session, done = std::move(done)](Result<void> fetched) mutable {
                     if (!fetched) return done(std::unexpected(std::move(fetched.error())));
                     const StoreEntry* entry = impl->find_entry(ref);
                     if (entry == nullptr) return done(std::unexpected(internal_bug("components: fetched runtime not stored")));
                     InstalledRuntime installed{.ref = ref,
                                                .kind = entry->runtime_kind,
                                                .root = impl->runtime_root(entry->archive.sha256)};
                     done(PinnedRuntime{.runtime = std::move(installed),
                                        .pin = ComponentPin(*this, impl->add_pin(ref, session), ref, session)});
                 });
}

void ComponentStore::hold(const PayloadSet& payload, PayloadRole role, CancelToken token, ProgressSink progress,
                          UniqueFunction<void(Result<IntegrityHold>)> done) {
    if (payload.find(role) == nullptr)
        return done(std::unexpected(internal_bug("components: hold of a role the payload set lacks")));
    impl_->hold_once(std::make_shared<Impl::HoldAttempt>(Impl::HoldAttempt{.payload = payload,
                                                                          .role = role,
                                                                          .token = std::move(token),
                                                                          .progress = std::move(progress),
                                                                          .done = std::move(done),
                                                                          .retried = false}));
}

void ComponentStore::mark_good(const ComponentPin& pin) {
    if (!pin.held()) return;
    const ComponentRef& ref = pin.ref();
    StoreEntry* good = impl_->find_entry(ref);
    if (good == nullptr) return;
    std::vector<ComponentRef> changed{ref};
    for (StoreEntry& entry : impl_->entries) {
        if (&entry == good || !entry.last_good || entry.ref.kind != ref.kind) continue;
        // One last good version per payload, and per runtime kind.
        if (ref.kind == ComponentKind::Runtime && entry.runtime_kind != good->runtime_kind) continue;
        entry.last_good = false;
        changed.push_back(entry.ref);
    }
    good->last_good = true;
    for (const ComponentRef& each : changed) impl_->publish_changed(each);
    impl_->write_index();
    impl_->request_maintenance();
}

void ComponentStore::unpin(u64 pin_id) noexcept {
    const auto found = impl_->pins.find(pin_id);
    if (found == impl_->pins.end()) return;
    const ComponentRef ref = found->second.ref;
    impl_->pins.erase(found);
    try {
        impl_->publish_changed(ref);
        impl_->request_maintenance();
    } catch (...) {
        REBOOT_LOG_ERROR(Update, "components: unpin bookkeeping failed");
    }
}

}  // namespace rb::components
