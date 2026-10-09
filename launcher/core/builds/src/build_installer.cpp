#include "reboot/builds/build_installer.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <span>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "build_support.hpp"
#include "builds_error.hpp"
#include "messages.hpp"
#include "op_tracker.hpp"
#include "path_text.hpp"
#include "reboot/builds/archive_extractor.hpp"
#include "reboot/builds/detect_version.hpp"
#include "reboot/builds/install_outcome.hpp"
#include "reboot/builds/install_phase.hpp"
#include "reboot/builds/layout_resolver.hpp"
#include "reboot/builds/library.hpp"
#include "reboot/builds/pe_version_reader.hpp"
#include "reboot/catalog/catalog_service.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/net/resumable_downloader.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/support/version_cap.hpp"

namespace rb::builds {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kArchiveName = "archive";
constexpr std::string_view kContentName = "content";
constexpr std::size_t kHashChunk = std::size_t{4} << 20;
constexpr std::array<std::string_view, 6> kFatTypes{"fat", "fat12", "fat16", "fat32", "vfat", "msdos"};
// net.download_http_status, whose "status" arg tells a withdrawn build from a failing host.
constexpr std::string_view kDownloadHttpStatus = "net.download_http_status";

[[nodiscard]] Diagnostic error(BuildsError error) { return to_diagnostic(error); }

[[nodiscard]] Diagnostic os_failure(BuildsErrorCode code, const NativePath& path, const std::error_code& ec) {
    return error(BuildsError{.code = code, .path = path, .os_error = SystemError{.code = ec.value()}});
}

[[nodiscard]] std::string_view phase(InstallPhase value) noexcept { return install_phase_name(value); }

[[nodiscard]] NativePath staging_of(const NativePath& destination) {
    return destination.parent_path() / kStagingDirName / destination.filename();
}

[[nodiscard]] NativePath archive_of(const NativePath& staging) { return staging / kArchiveName; }

[[nodiscard]] NativePath sidecar_of(const NativePath& archive) {
    return NativePath(archive) += net::kResumeSidecarSuffix;
}

[[nodiscard]] bool is_fat(std::string_view fs_type) {
    return std::ranges::any_of(kFatTypes, [&](std::string_view fat) { return iequals_ascii(fs_type, fat); });
}

// Why `volume` cannot hold `needed` bytes at `path`, if it cannot.
[[nodiscard]] std::optional<Diagnostic> volume_problem(const ports::VolumeInfo& volume, const NativePath& path,
                                                       u64 needed) {
    if (volume.read_only) return error(BuildsError{.code = BuildsErrorCode::VolumeReadOnly, .path = path});
    if (volume.network) return error(BuildsError{.code = BuildsErrorCode::VolumeNetwork, .path = path});
    if (volume.removable) return error(BuildsError{.code = BuildsErrorCode::VolumeRemovable, .path = path});
    if (is_fat(volume.fs_type))
        return error(BuildsError{.code = BuildsErrorCode::VolumeFat, .path = path, .fs_type = volume.fs_type});
    if (volume.free_bytes < needed) {
        return error(BuildsError{.code = BuildsErrorCode::InsufficientSpace,
                                 .path = path,
                                 .needed_bytes = needed,
                                 .free_bytes = volume.free_bytes});
    }
    return std::nullopt;
}

// Removes a file or tree that may be missing.
Result<void> remove_path(ports::IFileSystem& files, const NativePath& path) {
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(path, ec))) return {};
    Result<void> removed = files.remove_tree(path);
    if (removed || removed.error().kind == ErrorKind::NotFound) return {};
    return std::unexpected(
        error(BuildsError{.code = BuildsErrorCode::RemoveFailed, .path = path, .cause = std::move(removed.error())}));
}

// The staging folder of one destination, and the shared parent once nothing else is staged there.
Result<void> remove_staging(ports::IFileSystem& files, const NativePath& staging) {
    if (auto removed = remove_path(files, staging); !removed) return removed;
    std::error_code ec;
    fs::remove(staging.parent_path(), ec);
    return {};
}

// The volume query needs a path that exists; a destination often does not yet.
[[nodiscard]] NativePath existing_ancestor(const NativePath& path) {
    NativePath current = path;
    std::error_code ec;
    while (!fs::exists(current, ec) && current.has_relative_path()) current = current.parent_path();
    return current;
}

void drop_archive(const NativePath& archive) {
    std::error_code ec;
    fs::remove(archive, ec);
    fs::remove(sidecar_of(archive), ec);
}

[[nodiscard]] bool bad_archive(const Diagnostic& diag) {
    return diag.is(msg::kUnsupportedArchive) || diag.is(msg::kCorruptArchive) || diag.is(msg::kUnsafeEntryPath);
}

[[nodiscard]] std::optional<std::chrono::seconds> eta_of(u64 done, std::optional<u64> total, std::optional<u64> rate) {
    if (!total || !rate || *rate == 0 || *total < done) return std::nullopt;
    return std::chrono::seconds{(*total - done) / *rate};
}

// Worker progress reaches the strand one post at a time; later values replace a pending one.
class ProgressRelay {
public:
    template <class Deliver>
    void offer(Executor& strand, const Progress& progress, Deliver deliver) {
        const std::scoped_lock lock(mutex_);
        latest_ = progress;
        if (pending_) return;
        pending_ = true;
        strand.post([this, deliver = std::move(deliver)]() mutable { deliver(take()); });
    }

private:
    Progress take() {
        const std::scoped_lock lock(mutex_);
        pending_ = false;
        return latest_;
    }

    std::mutex mutex_;
    Progress latest_;
    bool pending_ = false;
};

struct Prepared {
    NativePath destination;
};

struct Detected {
    std::optional<BuildLayout> layout;
    std::optional<VersionDetection> detection;
    // Why the extracted files cannot be registered.
    std::optional<Diagnostic> unregistered;
};

}  // namespace

struct BuildInstaller::Impl {
    Impl(BuildInstallerDeps deps_in, InstallerConfig config_in)
        : deps(deps_in), config(std::move(config_in)), tracker(deps_in.ops) {}

    struct Job {
        Operation<InstallOutcome>& op;
        OpHandle handle;
        catalog::CatalogEntry entry;
        std::string name;
        std::vector<support::SupportRole> select_for;
        NativePath destination;
        NativePath staging;
        std::string key;
        // Set once the content was renamed into place.
        bool extracted = false;
        bool finished = false;
        // Shared with workers, which may still post after the job ended.
        std::shared_ptr<ProgressRelay> relay = std::make_shared<ProgressRelay>();
    };

    // Strand-side progress from a worker, dropped once the job ended.
    [[nodiscard]] UniqueFunction<void(const Progress&)> worker_progress(const std::shared_ptr<Job>& job) {
        return [&strand = deps.strand, alive = tracker.alive(), job, relay = job->relay](const Progress& progress) {
            relay->offer(strand, progress, [alive, job](Progress latest) {
                if (!alive.cancelled() && !job->finished) job->op.progress(latest);
            });
        };
    }

    void end(Job& job, Outcome<InstallOutcome> outcome) {
        if (job.finished) return;
        job.finished = true;
        installs.erase(job.key);
        if (job.extracted && !std::holds_alternative<Completed<InstallOutcome>>(outcome))
            unregistered.insert_or_assign(job.key, job.destination);
        if (const auto* completed = std::get_if<Completed<InstallOutcome>>(&outcome)) {
            if (const auto* left = std::get_if<UnregisteredInstall>(&completed->value))
                unregistered.insert_or_assign(job.key, left->folder);
        }
        tracker.finish(job.op, std::move(outcome));
    }

    void fail(const std::shared_ptr<Job>& job, Diagnostic diag) {
        // The extracted files are kept for "Import anyway" or "Delete files".
        if (job->extracted) {
            if (diag.kind == ErrorKind::Cancelled) return clean_up(job, Cancelled{});
            return clean_up(job, Completed<InstallOutcome>{.value = InstallOutcome(UnregisteredInstall{
                                                               .folder = job->destination, .reason = std::move(diag)})});
        }
        end(*job, failure<InstallOutcome>(std::move(diag)));
    }

    // True when a cancel or deadline already decided the outcome; the job then ends without the next step.
    bool stopped(const std::shared_ptr<Job>& job) {
        if (!job->op.token().cancelled()) return false;
        // Once extracted, the archive is no longer needed for a resume.
        if (job->extracted) clean_up(job, Cancelled{});
        else end(*job, Cancelled{});
        return true;
    }

    void prepare(const std::shared_ptr<Job>& job) {
        job->op.progress(Progress{.phase = phase(InstallPhase::Preparing)});
        const u64 installed = job->entry.installed_size.value_or(0);
        tracker.submit<Prepared>(
            deps.workers, deps.strand,
            [&disk = deps.disk, destination = job->destination, staging = job->staging,
             archive = job->entry.archive_size, installed,
             others = deps.library.list()](CancelToken token) -> Result<Prepared> {
                if (token.cancelled()) return std::unexpected(error(BuildsError{.code = BuildsErrorCode::Cancelled}));
                std::error_code ec;
                const fs::file_status status = fs::symlink_status(destination, ec);
                if (fs::exists(status)) {
                    if (!fs::is_directory(status) || !fs::is_empty(destination, ec) || ec) {
                        return std::unexpected(
                            error(BuildsError{.code = BuildsErrorCode::DestinationNotEmpty, .path = destination}));
                    }
                }
                Result<NativePath> canonical = canonical_root(destination, others);
                if (!canonical) return std::unexpected(std::move(canonical.error()));

                // A staged archive with its sidecar resumes, so its bytes are already on the volume.
                const NativePath staged_archive = archive_of(staging);
                u64 staged = 0;
                if (fs::exists(sidecar_of(staged_archive), ec)) {
                    const std::uintmax_t size = fs::file_size(staged_archive, ec);
                    if (!ec) staged = std::min<u64>(size, archive);
                }
                Result<ports::VolumeInfo> volume = disk.volume_of(existing_ancestor(destination));
                if (!volume) return std::unexpected(std::move(volume.error()));
                if (auto problem = volume_problem(*volume, destination, archive - staged + installed))
                    return std::unexpected(std::move(*problem));

                fs::create_directories(staging, ec);
                if (ec) return std::unexpected(os_failure(BuildsErrorCode::Io, staging, ec));
                return Prepared{.destination = std::move(*canonical)};
            },
            job->op.token(),
            [this, job](Result<Prepared> prepared) {
                if (!prepared) return fail(job, std::move(prepared.error()));
                if (auto checked = deps.library.check_root(prepared->destination, std::nullopt); !checked)
                    return fail(job, std::move(checked.error()));
                download(job);
            });
    }

    void download(const std::shared_ptr<Job>& job) {
        if (stopped(job)) return;
        const u64 size = job->entry.archive_size;
        job->op.progress(Progress{.phase = phase(InstallPhase::Downloading), .done = 0, .total = size});
        Result<void> started = deps.downloader.start(
            net::DownloadRequest{.url = job->entry.url,
                                 .file = archive_of(job->staging),
                                 .expected_size = size,
                                 .total_timeout = std::nullopt,
                                 .retry = {}},
            job->op.token(),
            [alive = tracker.alive(), job](const net::DownloadProgress& progress) {
                if (alive.cancelled() || job->finished) return;
                job->op.progress(Progress{.phase = phase(InstallPhase::Downloading),
                                          .done = progress.done,
                                          .total = progress.total,
                                          .rate_per_s = progress.bytes_per_s,
                                          .eta = eta_of(progress.done, progress.total, progress.bytes_per_s)});
            },
            [this, alive = tracker.alive(), job](Result<net::DownloadResult> downloaded) {
                if (alive.cancelled()) return;
                if (!downloaded) return fail(job, download_failure(*job, std::move(downloaded.error())));
                verify(job);
            });
        if (!started) fail(job, download_failure(*job, std::move(started.error())));
    }

    [[nodiscard]] static Diagnostic download_failure(const Job& job, Diagnostic cause) {
        if (cause.kind == ErrorKind::Cancelled) return cause;
        BuildsErrorCode code = BuildsErrorCode::DownloadFailed;
        if (cause.id == kDownloadHttpStatus) {
            if (const Arg* status = cause.find_arg("status")) {
                const auto* value = std::get_if<u64>(status);
                if (value != nullptr && (*value == 404 || *value == 410)) code = BuildsErrorCode::BuildUnavailable;
            }
        }
        return error(BuildsError{.code = code, .entry = job.entry.id, .cause = std::move(cause)});
    }

    void verify(const std::shared_ptr<Job>& job) {
        if (stopped(job)) return;
        if (!job->entry.sha256) return extract(job);
        job->op.progress(
            Progress{.phase = phase(InstallPhase::Verifying), .done = 0, .total = job->entry.archive_size});
        tracker.submit<void>(
            deps.workers, deps.strand,
            [archive = archive_of(job->staging), expected = *job->entry.sha256, entry = job->entry.id,
             progress = worker_progress(job)](CancelToken token) mutable -> Result<void> {
                std::error_code ec;
                const std::uintmax_t total = fs::file_size(archive, ec);
                std::ifstream in(archive, std::ios::binary);
                if (ec || !in) return std::unexpected(os_failure(BuildsErrorCode::Io, archive, ec));
                Sha256 hash;
                std::vector<u8> chunk(kHashChunk);
                u64 done = 0;
                while (in) {
                    if (token.cancelled())
                        return std::unexpected(error(BuildsError{.code = BuildsErrorCode::Cancelled}));
                    in.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
                    const auto got = static_cast<std::size_t>(in.gcount());
                    hash.update(std::span<const u8>(chunk.data(), got));
                    done += got;
                    progress(Progress{.phase = phase(InstallPhase::Verifying), .done = done, .total = total});
                }
                if (!in.eof()) return std::unexpected(error(BuildsError{.code = BuildsErrorCode::Io, .path = archive}));
                const std::array<u8, 32> actual = hash.finish();
                if (constant_time_equal(actual, expected)) return {};
                // Windows cannot delete a file that is still open.
                in.close();
                drop_archive(archive);
                return std::unexpected(error(BuildsError{.code = BuildsErrorCode::ChecksumMismatch, .entry = entry}));
            },
            job->op.token(),
            [this, job](Result<void> verified) {
                if (!verified) return fail(job, std::move(verified.error()));
                extract(job);
            });
    }

    void extract(const std::shared_ptr<Job>& job) {
        if (stopped(job)) return;
        job->op.progress(Progress{.phase = phase(InstallPhase::Extracting)});
        tracker.submit<ExtractSummary>(
            deps.workers, deps.strand,
            [&extractor = deps.extractor, &files = deps.fs, archive = archive_of(job->staging),
             content = job->staging / kContentName, destination = job->destination,
             progress = worker_progress(job)](CancelToken token) mutable -> Result<ExtractSummary> {
                // A partial content/ from an earlier attempt is never reused.
                if (auto cleared = remove_path(files, content); !cleared)
                    return std::unexpected(std::move(cleared.error()));
                std::error_code ec;
                fs::create_directories(content, ec);
                if (ec) return std::unexpected(os_failure(BuildsErrorCode::Io, content, ec));

                Result<ExtractSummary> summary = extractor.extract(
                    ExtractRequest{.archive = archive, .destination = content}, token,
                    [&progress](const ExtractProgress& extracted) {
                        progress(Progress{.phase = phase(InstallPhase::Extracting),
                                          .done = extracted.bytes_done,
                                          .total = extracted.bytes_total});
                    });
                if (!summary) {
                    (void)remove_path(files, content);
                    if (bad_archive(summary.error())) drop_archive(archive);
                    return summary;
                }
                for (const CaseCollision& collision : summary->case_collisions)
                    REBOOT_LOG_WARN(Builds, "the archive holds {} and {}; {} was kept", collision.kept,
                                    collision.replaced, collision.kept);

                // The one step that makes the build appear, all at once.
                if (fs::exists(fs::symlink_status(destination, ec))) {
                    fs::remove(destination, ec);
                    if (ec) {
                        (void)remove_path(files, content);
                        return std::unexpected(
                            error(BuildsError{.code = BuildsErrorCode::DestinationNotEmpty, .path = destination}));
                    }
                }
                fs::rename(content, destination, ec);
                if (ec) {
                    (void)remove_path(files, content);
                    return std::unexpected(os_failure(BuildsErrorCode::Io, destination, ec));
                }
                return summary;
            },
            job->op.token(),
            [this, job](Result<ExtractSummary> summary) {
                if (!summary) return fail(job, std::move(summary.error()));
                job->extracted = true;
                detect(job);
            });
    }

    void detect(const std::shared_ptr<Job>& job) {
        if (stopped(job)) return;
        job->op.progress(Progress{.phase = phase(InstallPhase::Detecting)});
        DetectionTables tables{.cl_table = deps.cl_table, .aliases = deps.catalog.aliases()};
        tracker.submit<Detected>(
            deps.workers, deps.strand,
            [resolver = resolver, reader = reader, destination = job->destination,
             tables = std::move(tables)](CancelToken token) -> Result<Detected> {
                Result<LayoutResolution> resolution = resolver.resolve(destination, std::nullopt, token);
                if (!resolution && resolution.error().kind == ErrorKind::Cancelled)
                    return std::unexpected(std::move(resolution.error()));
                if (!resolution) return Detected{.unregistered = std::move(resolution.error())};
                if (const auto* choice = std::get_if<NeedsShippingChoice>(&*resolution)) {
                    return Detected{.unregistered = error(BuildsError{
                                        .code = BuildsErrorCode::MultipleShipping,
                                        .path = destination,
                                        .count = static_cast<u32>(choice->candidates.size())})};
                }
                BuildLayout layout = std::get<BuildLayout>(std::move(*resolution));
                Result<VersionDetection> detection = detect_version(layout, reader, tables, token);
                if (!detection) return std::unexpected(std::move(detection.error()));
                return Detected{.layout = std::move(layout), .detection = std::move(*detection)};
            },
            job->op.token(),
            [this, job](Result<Detected> detected) {
                // A cancel that came after the walk must not register the build.
                if (stopped(job)) return;
                if (!detected) return fail(job, std::move(detected.error()));
                if (detected->unregistered) return fail(job, std::move(*detected->unregistered));
                register_build(job, std::move(*detected->layout), std::move(*detected->detection));
            });
    }

    void register_build(const std::shared_ptr<Job>& job, BuildLayout layout, VersionDetection detection) {
        job->op.progress(Progress{.phase = phase(InstallPhase::Registering)});
        const catalog::CatalogEntry& entry = job->entry;
        DetectedVersion version{.version = entry.version,
                                .cl = entry.changelist,
                                .source = VersionSource::Catalog,
                                .raw = entry.id,
                                .file = std::nullopt};
        if (auto* detected = std::get_if<DetectedVersion>(&detection)) {
            if (detected->version != entry.version) {
                REBOOT_LOG_WARN(Builds, "build {} is listed as {} but its files say {}", entry.id,
                                entry.version.canonical(), detected->version.canonical());
            }
            version = std::move(*detected);
        }
        if (support::above_version_cap(version.version)) {
            return fail(job,
                        error(BuildsError{.code = BuildsErrorCode::UnsupportedVersion, .version = version.version}));
        }
        Result<InstalledBuild> added = deps.library.add(NewBuild{.name = job->name,
                                                                 .root = layout.root,
                                                                 .version = std::move(version),
                                                                 .layout = layout,
                                                                 .catalog_entry = entry.id,
                                                                 .needs_relocation = false});
        if (!added) return fail(job, std::move(added.error()));
        for (const support::SupportRole role : job->select_for) {
            if (auto selected = deps.library.select(role, added->id); !selected)
                REBOOT_LOG_WARN(Builds, "could not select the installed build {}", added->name);
        }
        clean_up(job, Completed<InstallOutcome>{.value = InstallOutcome(std::move(*added))});
    }

    void clean_up(const std::shared_ptr<Job>& job, Outcome<InstallOutcome> outcome) {
        job->op.progress(Progress{.phase = phase(InstallPhase::CleaningUp)});
        tracker.submit<void>(
            deps.workers, deps.strand,
            [&files = deps.fs, staging = job->staging](CancelToken) { return remove_staging(files, staging); },
            CancelToken{},
            [this, job, outcome = std::move(outcome)](Result<void> removed) mutable {
                if (!removed) REBOOT_LOG_WARN(Builds, "could not remove the staging folder of {}", job->name);
                end(*job, std::move(outcome));
            });
    }

    [[nodiscard]] bool busy(const std::string& key) const { return installs.contains(key) || removals.contains(key); }

    BuildInstallerDeps deps;
    InstallerConfig config;
    OpTracker tracker;
    LayoutResolver resolver;
    PeVersionReader reader;
    // By folded_key of the destination.
    std::map<std::string, std::shared_ptr<Job>> installs;
    std::map<std::string, NativePath> unregistered;
    // Staging discards and folder deletions running, by the same key.
    std::map<std::string, OpHandle> removals;
};

BuildInstaller::BuildInstaller(BuildInstallerDeps deps, InstallerConfig config)
    : impl_(std::make_unique<Impl>(deps, std::move(config))) {}

BuildInstaller::~BuildInstaller() { impl_->tracker.shutdown(); }

Result<OpHandle> BuildInstaller::start_suggest_destination(std::string_view entry_name, DisconnectPolicy policy) {
    Result<catalog::CatalogEntry> entry = impl_->deps.catalog.installable_entry(entry_name);
    if (!entry) return std::unexpected(std::move(entry.error()));

    auto [handle, op] = impl_->deps.ops.create<DestinationSuggestion>(
        OpKind::Generic, policy, std::nullopt, RunnerMultiplier::Native,
        std::chrono::duration_cast<std::chrono::milliseconds>(kSuggestDestinationDeadline));
    impl_->tracker.track(op);
    impl_->tracker.submit<DestinationSuggestion>(
        impl_->deps.workers, impl_->deps.strand,
        [&disk = impl_->deps.disk, policy_kind = impl_->config.policy, data_root = impl_->config.data_root_builds_dir,
         entry = std::move(*entry)](CancelToken) -> Result<DestinationSuggestion> {
            DestinationSuggestion suggestion{.destination = {},
                                             .name = entry.id,
                                             .required_bytes = entry.archive_size + entry.installed_size.value_or(0),
                                             .required_bytes_complete = entry.installed_size.has_value(),
                                             .volumes = {}};
            const auto candidate = [&](ports::VolumeInfo volume, const NativePath& at) {
                std::optional<Diagnostic> problem = volume_problem(volume, at, suggestion.required_bytes);
                return VolumeCandidate{.volume = std::move(volume), .problem = std::move(problem)};
            };

            if (policy_kind == DestinationPolicy::LargestFittingVolume) {
                Result<std::vector<ports::VolumeInfo>> volumes = disk.volumes();
                if (!volumes) return std::unexpected(std::move(volumes.error()));
                for (ports::VolumeInfo& volume : *volumes) {
                    const NativePath at = volume.mount / kVolumeBuildsFolder / entry.id;
                    suggestion.volumes.push_back(candidate(std::move(volume), at));
                }
                // The most free space among volumes that fit, else among all, so the problem says why.
                const auto most_free = [&](bool fitting) {
                    const VolumeCandidate* best = nullptr;
                    for (const VolumeCandidate& volume : suggestion.volumes) {
                        if (fitting && volume.problem) continue;
                        if (best == nullptr || volume.volume.free_bytes > best->volume.free_bytes) best = &volume;
                    }
                    return best;
                };
                const VolumeCandidate* best = most_free(true);
                if (best == nullptr) best = most_free(false);
                if (best != nullptr) {
                    suggestion.destination = best->volume.mount / kVolumeBuildsFolder / entry.id;
                    return suggestion;
                }
            }

            suggestion.destination = data_root / entry.id;
            Result<ports::VolumeInfo> volume = disk.volume_of(data_root);
            if (!volume) return std::unexpected(std::move(volume.error()));
            suggestion.volumes.clear();
            suggestion.volumes.push_back(candidate(std::move(*volume), suggestion.destination));
            return suggestion;
        },
        op.token(),
        [impl = impl_.get(), &op](Result<DestinationSuggestion> suggestion) {
            if (!suggestion) {
                impl->tracker.finish(op, failure<DestinationSuggestion>(std::move(suggestion.error())));
                return;
            }
            impl->tracker.finish(op, Outcome<DestinationSuggestion>(
                                         Completed<DestinationSuggestion>{.value = std::move(*suggestion)}));
        });
    return handle;
}

Result<OpHandle> BuildInstaller::start_install(InstallRequest request, DisconnectPolicy policy) {
    Impl& impl = *impl_;
    Result<catalog::CatalogEntry> entry = impl.deps.catalog.installable_entry(request.entry);
    if (!entry) return std::unexpected(std::move(entry.error()));
    if (support::above_version_cap(entry->version))
        return std::unexpected(
            error(BuildsError{.code = BuildsErrorCode::UnsupportedVersion, .version = entry->version}));
    Result<std::string> name = impl.deps.library.check_name(request.name, std::nullopt);
    if (!name) return std::unexpected(std::move(name.error()));
    if (!request.destination.is_absolute()) {
        return std::unexpected(
            error(BuildsError{.code = BuildsErrorCode::PathNotAbsolute, .path = request.destination}));
    }
    const NativePath destination = normal_root(request.destination);
    // A volume root holds system files, and its staging folder would have no name.
    if (!destination.has_filename() || destination == destination.root_path()) {
        return std::unexpected(
            error(BuildsError{.code = BuildsErrorCode::DestinationNotEmpty, .path = destination}));
    }
    if (auto checked = impl.deps.library.check_root(destination, std::nullopt); !checked)
        return std::unexpected(std::move(checked.error()));

    const std::string key = folded_key(destination);
    if (const auto running = impl.installs.find(key); running != impl.installs.end()) {
        const Impl::Job& job = *running->second;
        // A cancelled install still winding down is not one to join.
        if (job.entry.id == entry->id && job.name == *name && !job.op.token().cancelled()) return job.handle;
        return std::unexpected(error(BuildsError{.code = BuildsErrorCode::DestinationBusy, .path = destination}));
    }
    if (impl.removals.contains(key))
        return std::unexpected(error(BuildsError{.code = BuildsErrorCode::DestinationBusy, .path = destination}));

    auto [handle, op] = impl.deps.ops.create<InstallOutcome>(OpKind::Install, policy, std::nullopt);
    impl.tracker.track(op);
    auto job = std::make_shared<Impl::Job>(Impl::Job{.op = op,
                                                     .handle = handle,
                                                     .entry = std::move(*entry),
                                                     .name = std::move(*name),
                                                     .select_for = std::move(request.select_for),
                                                     .destination = destination,
                                                     .staging = staging_of(destination),
                                                     .key = key});
    impl.installs.emplace(key, job);
    impl.prepare(job);
    return handle;
}

Result<OpHandle> BuildInstaller::start_discard_staging(const NativePath& destination_in, DisconnectPolicy policy) {
    Impl& impl = *impl_;
    if (!destination_in.is_absolute())
        return std::unexpected(error(BuildsError{.code = BuildsErrorCode::PathNotAbsolute, .path = destination_in}));
    const NativePath destination = normal_root(destination_in);
    // No install stages for a volume root, and its staging path would name every destination's archive.
    if (!destination.has_filename())
        return std::unexpected(error(BuildsError{.code = BuildsErrorCode::DestinationNotEmpty, .path = destination}));
    const std::string key = folded_key(destination);
    if (impl.busy(key))
        return std::unexpected(error(BuildsError{.code = BuildsErrorCode::DestinationBusy, .path = destination}));

    auto [handle, op] = impl.deps.ops.create<void>(OpKind::Generic, policy, std::nullopt);
    impl.tracker.track(op);
    impl.removals.emplace(key, handle);
    impl.tracker.submit<void>(
        impl.deps.workers, impl.deps.strand,
        [&files = impl.deps.fs, staging = staging_of(destination)](CancelToken) {
            return remove_staging(files, staging);
        },
        op.token(),
        [&impl, key, &op](Result<void> removed) {
            impl.removals.erase(key);
            if (!removed) impl.tracker.finish(op, failure<void>(std::move(removed.error())));
            else impl.tracker.finish(op, Outcome<void>(Completed<void>{}));
        });
    return handle;
}

Result<OpHandle> BuildInstaller::start_delete_unregistered(const NativePath& folder_in, DisconnectPolicy policy) {
    Impl& impl = *impl_;
    const NativePath folder = normal_root(folder_in);
    const std::string key = folded_key(folder);
    const auto unknown = [&] {
        return std::unexpected(error(BuildsError{.code = BuildsErrorCode::UnknownInstallFolder, .path = folder}));
    };
    const auto left = impl.unregistered.find(key);
    if (left == impl.unregistered.end()) return unknown();
    // "Import anyway" may have registered it since.
    for (const InstalledBuild& build : impl.deps.library.list())
        if (is_inside(build.root, left->second)) return unknown();
    if (const auto running = impl.removals.find(key); running != impl.removals.end()) return running->second;
    if (impl.installs.contains(key))
        return std::unexpected(error(BuildsError{.code = BuildsErrorCode::DestinationBusy, .path = folder}));

    auto [handle, op] = impl.deps.ops.create<void>(OpKind::Generic, policy, std::nullopt, RunnerMultiplier::Native,
                                                   std::chrono::milliseconds(kRemoveDeadline));
    impl.tracker.track(op);
    impl.removals.emplace(key, handle);
    impl.tracker.submit<void>(
        impl.deps.workers, impl.deps.strand,
        [&files = impl.deps.fs, target = left->second](CancelToken token) -> Result<void> {
            if (token.cancelled()) return std::unexpected(error(BuildsError{.code = BuildsErrorCode::Cancelled}));
            return remove_path(files, target);
        },
        op.token(),
        [&impl, key, &op](Result<void> removed) {
            impl.removals.erase(key);
            if (!removed) {
                impl.tracker.finish(op, failure<void>(std::move(removed.error())));
                return;
            }
            impl.unregistered.erase(key);
            impl.tracker.finish(op, Outcome<void>(Completed<void>{}));
        });
    return handle;
}

}  // namespace rb::builds
