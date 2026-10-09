#include "reboot/builds/import_service.hpp"

#include <any>
#include <filesystem>
#include <format>
#include <system_error>
#include <utility>
#include <variant>

#include "build_support.hpp"
#include "builds_error.hpp"
#include "op_tracker.hpp"
#include "reboot/builds/choose_version_prompt.hpp"
#include "reboot/builds/detect_version.hpp"
#include "reboot/builds/import_outcome.hpp"
#include "reboot/builds/library.hpp"
#include "reboot/catalog/catalog_service.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/support/version_cap.hpp"

namespace reboot::builds {

namespace {

[[nodiscard]] Diagnostic error(BuildsError error) { return to_diagnostic(error); }

[[nodiscard]] Result<void> within_cap(const GameVersion& version) {
    if (!support::above_version_cap(version)) return {};
    return std::unexpected(error(BuildsError{.code = BuildsErrorCode::UnsupportedVersion, .version = version}));
}

[[nodiscard]] DetectedVersion user_version(const UserVersion& version) {
    return DetectedVersion{.version = version.version,
                           .cl = version.cl,
                           .source = VersionSource::User,
                           .raw = version.version.canonical(),
                           .file = std::nullopt};
}

struct Resolved {
    std::optional<NeedsShippingChoice> choice;
    std::optional<BuildLayout> layout;
    std::optional<VersionDetection> detection;
};

}  // namespace

struct ImportService::Impl {
    explicit Impl(ImportServiceDeps& deps_ref) : deps(deps_ref), tracker(deps_ref.ops) {}

    struct Job {
        Operation<ImportOutcome>& op;
        ImportRequest request;
        std::string name;
        std::optional<catalog::CatalogEntry> catalog_entry;
        Resolved resolved;
        bool finished = false;
        CancelRegistration on_cancel;
    };

    void finish(Job& job, Outcome<ImportOutcome> outcome) {
        // Exactly once: the op may be freed after its work completed it.
        if (job.finished) return;
        job.finished = true;
        job.on_cancel.reset();
        tracker.finish(job.op, std::move(outcome));
    }

    void fail(Job& job, Diagnostic diag) { finish(job, failure<ImportOutcome>(std::move(diag))); }

    void complete(Job& job, ImportOutcome outcome) {
        finish(job, Outcome<ImportOutcome>(Completed<ImportOutcome>{.value = std::move(outcome)}));
    }

    void on_resolved(const std::shared_ptr<Job>& job) {
        Resolved& resolved = job->resolved;
        if (resolved.choice) return complete(*job, ImportOutcome(std::move(*resolved.choice)));
        if (auto checked = deps.library.check_root(resolved.layout->root, std::nullopt); !checked)
            return fail(*job, std::move(checked.error()));

        const auto* detected = std::get_if<DetectedVersion>(&*resolved.detection);
        if (job->request.version) {
            if (detected && detected->version != job->request.version->version) {
                REBOOT_LOG_INFO(Builds, "import keeps the stated version {} over the detected {}",
                                job->request.version->version.canonical(), detected->version.canonical());
            }
            return register_build(job, user_version(*job->request.version));
        }
        if (detected) return register_build(job, *detected);
        if (job->catalog_entry) {
            return register_build(job, DetectedVersion{.version = job->catalog_entry->version,
                                                       .cl = job->catalog_entry->changelist,
                                                       .source = VersionSource::Catalog,
                                                       .raw = job->catalog_entry->id,
                                                       .file = std::nullopt});
        }
        NeedsUserVersion needs = std::get<NeedsUserVersion>(std::move(*resolved.detection));
        if (!job->request.ask_user) return complete(*job, ImportOutcome(std::move(needs)));
        ask_version(job, std::move(needs));
    }

    void ask_version(const std::shared_ptr<Job>& job, NeedsUserVersion needs) {
        ChooseVersionPrompt prompt{.name = job->name,
                                   .root = job->resolved.layout->root,
                                   .raw = needs.raw,
                                   .reasons = std::move(needs.reasons)};
        const CancelToken token = job->op.token();
        const RequestId request = deps.requests.ask(
            UserRequestKind::ChooseVersion, std::move(prompt), job->op.id(), std::nullopt,
            [this, alive = tracker.alive(), job](const std::any& answer) -> Result<void> {
                if (alive.cancelled()) return std::unexpected(internal_bug("builds.choose_version_after_shutdown"));
                const auto* version = std::any_cast<UserVersion>(&answer);
                if (version == nullptr) return std::unexpected(internal_bug("builds.choose_version_answer"));
                if (auto capped = within_cap(version->version); !capped) return capped;
                // Continues after respond() returned, outside the registry's call.
                deps.strand.post([this, alive, job, chosen = *version] {
                    if (alive.cancelled() || job->finished) return;
                    job->on_cancel.reset();
                    register_build(job, user_version(chosen));
                });
                return {};
            },
            token);
        job->op.awaiting_user(request);
        // A cancel withdraws the request, so nothing else would complete the op.
        job->on_cancel = token.on_cancel([this, alive = tracker.alive(), job](CancelReason) {
            deps.strand.post([this, alive, job] {
                if (!alive.cancelled()) finish(*job, Cancelled{});
            });
        });
    }

    void register_build(const std::shared_ptr<Job>& job, DetectedVersion version) {
        // A cancel posted behind the step that got here must not leave the build added.
        if (job->op.token().cancelled()) return finish(*job, Cancelled{});
        job->op.progress(Progress{.phase = "registering"});
        if (auto capped = within_cap(version.version); !capped) return fail(*job, std::move(capped.error()));
        Result<InstalledBuild> added = deps.library.add(NewBuild{.name = job->name,
                                                                 .root = job->resolved.layout->root,
                                                                 .version = std::move(version),
                                                                 .layout = job->resolved.layout,
                                                                 .catalog_entry = job->request.catalog_entry,
                                                                 .needs_relocation = false});
        if (!added) return fail(*job, std::move(added.error()));
        complete(*job, ImportOutcome(Imported{.build = std::move(*added)}));
    }

    ImportServiceDeps& deps;
    OpTracker tracker;
};

ImportService::ImportService(ImportServiceDeps deps) : deps_(deps), impl_(std::make_unique<Impl>(deps_)) {}

ImportService::~ImportService() { impl_->tracker.shutdown(); }

std::string ImportService::suggest_name(const NativePath& path) const {
    std::string base = trim_ascii(display_utf8(normal_root(path).filename()));
    if (base.empty()) base = "Fortnite";
    if (deps_.library.check_name(base, std::nullopt)) return base;
    for (u32 suffix = 1;; ++suffix) {
        std::string candidate = std::format("{}-{}", base, suffix);
        if (deps_.library.check_name(candidate, std::nullopt)) return candidate;
    }
}

Result<OpHandle> ImportService::start_import(ImportRequest request, DisconnectPolicy policy) {
    if (!request.path.is_absolute())
        return std::unexpected(error(BuildsError{.code = BuildsErrorCode::PathNotAbsolute, .path = request.path}));
    Result<std::string> name = deps_.library.check_name(request.name, std::nullopt);
    if (!name) return std::unexpected(std::move(name.error()));
    if (auto checked = deps_.library.check_root(request.path, std::nullopt); !checked)
        return std::unexpected(std::move(checked.error()));
    if (request.version) {
        if (auto capped = within_cap(request.version->version); !capped)
            return std::unexpected(std::move(capped.error()));
    }
    std::optional<catalog::CatalogEntry> catalog_entry;
    if (request.catalog_entry) {
        Result<catalog::CatalogEntry> entry = deps_.catalog.entry(*request.catalog_entry);
        if (!entry) return std::unexpected(std::move(entry.error()));
        catalog_entry = std::move(*entry);
    }

    auto [handle, op] = deps_.ops.create<ImportOutcome>(OpKind::Import, policy, std::nullopt);
    impl_->tracker.track(op);
    auto job = std::make_shared<Impl::Job>(Impl::Job{.op = op,
                                                     .request = std::move(request),
                                                     .name = std::move(*name),
                                                     .catalog_entry = std::move(catalog_entry),
                                                     .resolved = {},
                                                     .finished = false,
                                                     .on_cancel = {}});
    op.progress(Progress{.phase = "detecting"});
    DetectionTables tables{.cl_table = deps_.cl_table, .aliases = deps_.catalog.aliases()};
    impl_->tracker.submit<Resolved>(
        deps_.workers, deps_.strand,
        // Workers may outlive the service, so they hold copies, never `this`.
        [resolver = resolver_, reader = reader_, path = job->request.path, shipping = job->request.shipping_exe,
         others = deps_.library.list(), tables = std::move(tables)](CancelToken token) -> Result<Resolved> {
            std::error_code ec;
            const auto status = std::filesystem::status(path, ec);
            if (!std::filesystem::exists(status))
                return std::unexpected(error(BuildsError{.code = BuildsErrorCode::PathMissing, .path = path}));
            if (!std::filesystem::is_directory(status))
                return std::unexpected(error(BuildsError{.code = BuildsErrorCode::NotADirectory, .path = path}));

            Result<LayoutResolution> resolution = resolver.resolve(path, shipping, token);
            if (!resolution) return std::unexpected(std::move(resolution.error()));
            if (auto* choice = std::get_if<NeedsShippingChoice>(&*resolution))
                return Resolved{.choice = std::move(*choice), .layout = std::nullopt, .detection = std::nullopt};
            BuildLayout layout = std::get<BuildLayout>(std::move(*resolution));
            Result<NativePath> canonical = canonical_root(layout.root, others);
            if (!canonical) return std::unexpected(std::move(canonical.error()));
            layout.root = std::move(*canonical);

            Result<VersionDetection> detection = detect_version(layout, reader, tables, token);
            if (!detection) return std::unexpected(std::move(detection.error()));
            return Resolved{.choice = std::nullopt, .layout = std::move(layout), .detection = std::move(*detection)};
        },
        op.token(),
        [impl = impl_.get(), job](Result<Resolved> resolved) {
            if (!resolved) return impl->fail(*job, std::move(resolved.error()));
            job->resolved = std::move(*resolved);
            impl->on_resolved(job);
        });
    return handle;
}

}  // namespace reboot::builds
