#include "reboot/builds/library.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <unordered_map>
#include <utility>

#include "build_support.hpp"
#include "builds_error.hpp"
#include "messages.hpp"
#include "op_tracker.hpp"
#include "reboot/builds/build_usage.hpp"
#include "reboot/builds/detect_version.hpp"
#include "reboot/builds/layout_resolver.hpp"
#include "reboot/builds/library_changed_event.hpp"
#include "reboot/builds/pe_version_reader.hpp"
#include "reboot/catalog/catalog_service.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::builds {

namespace {

using storage::LibraryDocument;
using storage::LibraryEntry;

[[nodiscard]] Diagnostic error(BuildsError error) { return to_diagnostic(error); }

[[nodiscard]] Diagnostic not_found(BuildId id) {
    return error(BuildsError{.code = BuildsErrorCode::NotFound, .build = id});
}

[[nodiscard]] std::optional<BuildId>& selection_of(LibraryDocument& document, support::SupportRole role) {
    return role == support::SupportRole::Play ? document.client_selection : document.host_selection;
}

[[nodiscard]] const std::optional<BuildId>& selection_of(const LibraryDocument& document, support::SupportRole role) {
    return role == support::SupportRole::Play ? document.client_selection : document.host_selection;
}

[[nodiscard]] LibraryEntry* find_entry(LibraryDocument& document, BuildId id) {
    const auto it = std::ranges::find(document.builds, id, &LibraryEntry::id);
    return it == document.builds.end() ? nullptr : &*it;
}

void set_version(LibraryEntry& entry, const DetectedVersion& version) {
    entry.version = version.version;
    entry.changelist = version.cl;
    entry.version_source = std::string(version_source_name(version.source));
}

struct Relocated {
    NativePath root;
    BuildLayout layout;
    VersionDetection detection;
};

}  // namespace

struct Library::Impl {
    explicit Impl(LibraryDeps deps_in) : deps(deps_in), tracker(deps_in.ops) {}

    [[nodiscard]] const LibraryDocument& document() const noexcept { return deps.store.get(); }

    [[nodiscard]] const LibraryEntry* find(BuildId id) const {
        const auto& builds = document().builds;
        const auto it = std::ranges::find(builds, id, &LibraryEntry::id);
        return it == builds.end() ? nullptr : &*it;
    }

    [[nodiscard]] InstalledBuild to_build(const LibraryEntry& entry) const {
        const auto known = presence.find(entry.id);
        std::optional<VersionSource> source;
        if (entry.version && entry.version_source) source = version_source_from_name(*entry.version_source);
        return InstalledBuild{.id = entry.id,
                              .name = entry.name,
                              .root = entry.root,
                              .version = entry.version,
                              .cl = entry.changelist,
                              .version_source = source,
                              .catalog_entry = entry.catalog_entry,
                              .added_at = entry.added_at,
                              .needs_relocation = entry.needs_relocation,
                              .presence = known == presence.end() ? BuildPresence::Unchecked : known->second};
    }

    [[nodiscard]] std::vector<InstalledBuild> others(BuildId except) const {
        std::vector<InstalledBuild> out;
        for (const LibraryEntry& entry : document().builds)
            if (entry.id != except) out.push_back(to_build(entry));
        return out;
    }

    void publish(LibraryChange change, std::optional<BuildId> build) {
        deps.events.publish(EventKind::LibraryChanged,
                            LibraryChangedEvent{.change = change, .build = build, .revision = deps.store.revision()},
                            EventScope{.session = std::nullopt, .op = std::nullopt,
                                       .coalesce_key = std::string(kLibraryCoalesceKey)});
    }

    // builds.in_use while sessions run the build and `running` says to refuse.
    [[nodiscard]] Result<bool> must_stop(const LibraryEntry& entry, RunningPolicy running) const {
        const std::vector<SessionId> sessions = deps.usage.sessions_using(entry.id);
        if (sessions.empty()) return false;
        if (running == RunningPolicy::StopSessions) return true;
        return std::unexpected(error(BuildsError{.code = BuildsErrorCode::InUse,
                                                 .name = entry.name,
                                                 .count = static_cast<u32>(sessions.size())}));
    }

    // Runs `next` once no session uses the build, stopping them first when asked.
    template <class T>
    void after_sessions(BuildId id, bool stop, Operation<T>& op, UniqueFunction<void()> next) {
        if (!stop) return next();
        deps.usage.stop_sessions_using(
            id, [this, alive = tracker.alive(), &op, next = std::move(next)](Result<void> stopped) mutable {
                if (alive.cancelled()) return;
                if (!stopped) {
                    tracker.finish(op, failure<T>(std::move(stopped.error())));
                    return;
                }
                next();
            });
    }

    void relocate(BuildId id, NativePath root, Operation<InstalledBuild>& op) {
        op.progress(Progress{.phase = "detecting"});
        DetectionTables tables{.cl_table = deps.cl_table, .aliases = deps.catalog.aliases()};
        tracker.submit<Relocated>(
            deps.workers, deps.strand,
            // Workers may outlive the service, so they hold copies, never `this`.
            [resolver = resolver, reader = reader, root, others = others(id),
             tables = std::move(tables)](CancelToken token) -> Result<Relocated> {
                Result<NativePath> canonical = canonical_root(root, others);
                if (!canonical) return std::unexpected(std::move(canonical.error()));
                Result<LayoutResolution> resolution = resolver.resolve(*canonical, std::nullopt, token);
                if (!resolution) return std::unexpected(std::move(resolution.error()));
                if (const auto* choice = std::get_if<NeedsShippingChoice>(&*resolution)) {
                    return std::unexpected(error(BuildsError{.code = BuildsErrorCode::MultipleShipping,
                                                             .path = *canonical,
                                                             .count = static_cast<u32>(choice->candidates.size())}));
                }
                BuildLayout layout = std::get<BuildLayout>(std::move(*resolution));
                Result<VersionDetection> detection = detect_version(layout, reader, tables, token);
                if (!detection) return std::unexpected(std::move(detection.error()));
                return Relocated{.root = layout.root, .layout = std::move(layout), .detection = std::move(*detection)};
            },
            op.token(),
            [this, id, &op](Result<Relocated> relocated) {
                if (!relocated) {
                    tracker.finish(op, failure<InstalledBuild>(std::move(relocated.error())));
                    return;
                }
                // Cancelled after the walk: the entry keeps its old root.
                if (op.token().cancelled()) {
                    tracker.finish(op, Outcome<InstalledBuild>(Cancelled{}));
                    return;
                }
                Result<InstalledBuild> moved = apply_relocation(id, std::move(*relocated));
                if (!moved) tracker.finish(op, failure<InstalledBuild>(std::move(moved.error())));
                else tracker.finish(op, Outcome<InstalledBuild>(Completed<InstalledBuild>{.value = std::move(*moved)}));
            });
    }

    Result<InstalledBuild> apply_relocation(BuildId id, Relocated relocated) {
        const LibraryEntry* entry = find(id);
        if (entry == nullptr) return std::unexpected(not_found(id));
        if (auto checked = check_root(relocated.root, id); !checked) return std::unexpected(std::move(checked.error()));

        const auto* detected = std::get_if<DetectedVersion>(&relocated.detection);
        const bool confirmed = entry->version && entry->version_source &&
                               version_source_from_name(*entry->version_source).has_value();
        if (detected && confirmed && detected->version != *entry->version) {
            return std::unexpected(error(BuildsError{.code = BuildsErrorCode::VersionMismatch,
                                                     .path = relocated.root,
                                                     .version = entry->version,
                                                     .found_version = detected->version}));
        }
        Result<u64> written = deps.store.update([&](LibraryDocument& document) {
            LibraryEntry* target = find_entry(document, id);
            if (target == nullptr) return;
            target->root = relocated.root;
            target->needs_relocation = false;
            target->layout = to_stored_layout(relocated.layout);
            if (detected && !confirmed) set_version(*target, *detected);
        });
        if (!written) return std::unexpected(std::move(written.error()));
        presence[id] = BuildPresence::Present;
        publish(LibraryChange::Updated, id);
        return to_build(*find(id));
    }

    [[nodiscard]] Result<void> check_root(const NativePath& root, std::optional<BuildId> except) const {
        if (!root.is_absolute())
            return std::unexpected(error(BuildsError{.code = BuildsErrorCode::PathNotAbsolute, .path = root}));
        const NativePath normal = normal_root(root);
        if (!deps.install.install_dir.empty() && is_inside(normal, deps.install.install_dir))
            return std::unexpected(error(BuildsError{.code = BuildsErrorCode::InsideInstallDir, .path = root}));
        for (const LibraryEntry& entry : document().builds) {
            if (except && entry.id == *except) continue;
            const bool below = is_inside(normal, entry.root);
            const bool above = is_inside(entry.root, normal);
            if (below && above) {
                return std::unexpected(
                    error(BuildsError{.code = BuildsErrorCode::AlreadyRegistered, .path = root, .name = entry.name}));
            }
            if (below || above) {
                return std::unexpected(
                    error(BuildsError{.code = BuildsErrorCode::OverlapsBuild, .path = root, .name = entry.name}));
            }
        }
        return {};
    }

    void remove(BuildId id, RemoveFiles files, Operation<void>& op) {
        const LibraryEntry* entry = find(id);
        if (entry == nullptr) {
            tracker.finish(op, failure<void>(not_found(id)));
            return;
        }
        op.progress(Progress{.phase = "removing"});
        tracker.submit<void>(
            deps.workers, deps.strand,
            [&shell = deps.shell, &fs = deps.fs, files, root = entry->root](CancelToken token) -> Result<void> {
                if (token.cancelled()) return std::unexpected(error(BuildsError{.code = BuildsErrorCode::Cancelled}));
                Result<void> handled;
                if (files == RemoveFiles::Trash) handled = shell.trash(root);
                else if (files == RemoveFiles::Delete) handled = fs.remove_tree(root);
                // A root that is already gone needs no handling.
                if (handled || handled.error().kind == ErrorKind::NotFound) return {};
                return std::unexpected(error(BuildsError{
                    .code = BuildsErrorCode::RemoveFailed, .path = root, .cause = std::move(handled.error())}));
            },
            op.token(),
            [this, id, &op](Result<void> handled) {
                if (!handled) {
                    tracker.finish(op, failure<void>(std::move(handled.error())));
                    return;
                }
                // The files are handled, so the entry goes even when the op was cancelled meanwhile.
                Result<u64> written = deps.store.update([id](LibraryDocument& document) {
                    std::erase_if(document.builds, [id](const LibraryEntry& listed) { return listed.id == id; });
                    if (document.client_selection == id) document.client_selection.reset();
                    if (document.host_selection == id) document.host_selection.reset();
                });
                if (!written) {
                    tracker.finish(op, failure<void>(std::move(written.error())));
                    return;
                }
                presence.erase(id);
                publish(LibraryChange::Removed, id);
                tracker.finish(op, Outcome<void>(Completed<void>{}));
            });
    }

    LibraryDeps deps;
    OpTracker tracker;
    LayoutResolver resolver;
    PeVersionReader reader;
    std::unordered_map<BuildId, BuildPresence> presence;
};

Library::Library(LibraryDeps deps) : impl_(std::make_unique<Impl>(deps)) {}

Library::~Library() { impl_->tracker.shutdown(); }

std::vector<InstalledBuild> Library::list() const {
    std::vector<InstalledBuild> out;
    for (const LibraryEntry& entry : impl_->document().builds) out.push_back(impl_->to_build(entry));
    return out;
}

Result<InstalledBuild> Library::get(BuildId id) const {
    const LibraryEntry* entry = impl_->find(id);
    if (entry == nullptr) return std::unexpected(not_found(id));
    return impl_->to_build(*entry);
}

std::optional<BuildId> Library::selected(support::SupportRole role) const {
    const std::optional<BuildId>& selection = selection_of(impl_->document(), role);
    if (!selection || impl_->find(*selection) == nullptr) return std::nullopt;
    return selection;
}

Result<void> Library::select(support::SupportRole role, std::optional<BuildId> id) {
    if (id && impl_->find(*id) == nullptr) return std::unexpected(not_found(*id));
    if (selected(role) == id) return {};
    Result<u64> written = impl_->deps.store.update(
        [role, id](LibraryDocument& document) { selection_of(document, role) = id; });
    if (!written) return std::unexpected(std::move(written.error()));
    impl_->publish(LibraryChange::SelectionChanged, id);
    return {};
}

Result<InstalledBuild> Library::update(BuildId id, const BuildPatch& patch) {
    if (impl_->find(id) == nullptr) return std::unexpected(not_found(id));
    std::optional<std::string> name;
    if (patch.name) {
        Result<std::string> checked = check_name(*patch.name, id);
        if (!checked) return std::unexpected(std::move(checked.error()));
        name = std::move(*checked);
    }
    Result<u64> written = impl_->deps.store.update([&](LibraryDocument& document) {
        LibraryEntry* entry = find_entry(document, id);
        if (entry == nullptr) return;
        if (name) entry->name = *name;
        if (patch.version) {
            entry->version = patch.version;
            entry->version_source = std::string(version_source_name(VersionSource::User));
        }
        if (patch.cl) entry->changelist = patch.cl;
    });
    if (!written) return std::unexpected(std::move(written.error()));
    impl_->publish(LibraryChange::Updated, id);
    return impl_->to_build(*impl_->find(id));
}

Result<OpHandle> Library::start_relocate(BuildId id, NativePath root, RunningPolicy running, DisconnectPolicy policy) {
    const LibraryEntry* entry = impl_->find(id);
    if (entry == nullptr) return std::unexpected(not_found(id));
    if (auto checked = check_root(root, id); !checked) return std::unexpected(std::move(checked.error()));
    Result<bool> stop = impl_->must_stop(*entry, running);
    if (!stop) return std::unexpected(std::move(stop.error()));

    auto [handle, op] = impl_->deps.ops.create<InstalledBuild>(OpKind::Import, policy, std::nullopt);
    impl_->tracker.track(op);
    impl_->after_sessions(id, *stop, op, [impl = impl_.get(), id, root = normal_root(root), &op]() mutable {
        impl->relocate(id, std::move(root), op);
    });
    return handle;
}

Result<std::string> Library::check_name(std::string_view name, std::optional<BuildId> except) const {
    std::string trimmed = trim_ascii(name);
    if (trimmed.empty()) return std::unexpected(error(BuildsError{.code = BuildsErrorCode::NameEmpty}));
    for (const LibraryEntry& entry : impl_->document().builds) {
        if (except && entry.id == *except) continue;
        if (iequals_ascii(entry.name, trimmed))
            return std::unexpected(error(BuildsError{.code = BuildsErrorCode::NameTaken, .name = trimmed}));
    }
    return trimmed;
}

Result<void> Library::check_root(const NativePath& root, std::optional<BuildId> except) const {
    return impl_->check_root(root, except);
}

Result<InstalledBuild> Library::add(NewBuild build) {
    Result<std::string> name = check_name(build.name, std::nullopt);
    if (!name) return std::unexpected(std::move(name.error()));
    if (auto checked = check_root(build.root, std::nullopt); !checked)
        return std::unexpected(std::move(checked.error()));

    LibraryEntry entry{.id = BuildId{uuid_v4(impl_->deps.random)},
                       .name = std::move(*name),
                       .root = normal_root(build.root),
                       .catalog_entry = std::move(build.catalog_entry),
                       .added_at = impl_->deps.clock.system_now(),
                       .needs_relocation = build.needs_relocation};
    if (build.version) set_version(entry, *build.version);
    if (build.layout) entry.layout = to_stored_layout(*build.layout);
    const BuildId id = entry.id;
    Result<u64> written =
        impl_->deps.store.update([&entry](LibraryDocument& document) { document.builds.push_back(entry); });
    if (!written) return std::unexpected(std::move(written.error()));
    if (build.layout) impl_->presence[id] = BuildPresence::Present;
    impl_->publish(LibraryChange::Added, id);
    return impl_->to_build(*impl_->find(id));
}

Result<OpHandle> Library::start_remove(BuildId id, RemoveFiles files, RunningPolicy running, DisconnectPolicy policy) {
    const LibraryEntry* entry = impl_->find(id);
    if (entry == nullptr) return std::unexpected(not_found(id));
    Result<bool> stop = impl_->must_stop(*entry, running);
    if (!stop) return std::unexpected(std::move(stop.error()));

    auto [handle, op] = impl_->deps.ops.create<void>(OpKind::Generic, policy, std::nullopt, RunnerMultiplier::Native,
                                                     std::chrono::milliseconds(kRemoveDeadline));
    impl_->tracker.track(op);
    impl_->after_sessions(id, *stop, op, [impl = impl_.get(), id, files, &op] { impl->remove(id, files, op); });
    return handle;
}

std::vector<InstalledBuild> Library::find_compatible(const GameVersion& version) const {
    std::vector<InstalledBuild> exact;
    std::vector<InstalledBuild> bucket;
    for (InstalledBuild& build : list()) {
        if (!build.version_confirmed()) continue;
        if (*build.version == version) exact.push_back(std::move(build));
        else if (build.version->bucket() == version.bucket()) bucket.push_back(std::move(build));
    }
    const auto newer_added = [](const InstalledBuild& a, const InstalledBuild& b) { return a.added_at > b.added_at; };
    std::ranges::stable_sort(exact, newer_added);
    std::ranges::stable_sort(bucket, [&](const InstalledBuild& a, const InstalledBuild& b) {
        if (*a.version != *b.version) return *a.version > *b.version;
        return newer_added(a, b);
    });
    exact.insert(exact.end(), std::make_move_iterator(bucket.begin()), std::make_move_iterator(bucket.end()));
    return exact;
}

std::vector<InstalledBuild> Library::find_compatible(std::string_view remote_version) const {
    const Result<GameVersion> version = GameVersion::parse(remote_version);
    if (!version) return {};
    return find_compatible(*version);
}

void Library::resolve_layout(BuildId id, CancelToken token, UniqueFunction<void(Result<BuildLayout>)> done) {
    Impl& impl = *impl_;
    const LibraryEntry* entry = impl.find(id);
    if (entry == nullptr) {
        impl.deps.strand.post([alive = impl.tracker.alive(), id, done = std::move(done)]() mutable {
            if (!alive.cancelled()) done(std::unexpected(not_found(id)));
        });
        return;
    }
    std::optional<BuildLayout> stored;
    if (entry->layout) stored = from_stored_layout(entry->root, *entry->layout);
    impl.tracker.submit<BuildLayout>(
        impl.deps.workers, impl.deps.strand,
        [resolver = impl.resolver, root = entry->root, stored](CancelToken worker_token) -> Result<BuildLayout> {
            if (stored && resolver.still_valid(*stored)) return *stored;
            std::optional<NativePath> chosen;
            if (stored) chosen = (stored->root / stored->shipping_exe).lexically_relative(root);
            Result<LayoutResolution> resolution = resolver.resolve(root, chosen, worker_token);
            // The exe picked earlier is gone; a fresh walk may still settle one.
            if (!resolution && chosen && resolution.error().is(msg::kShippingNotFound))
                resolution = resolver.resolve(root, std::nullopt, worker_token);
            if (!resolution) return std::unexpected(std::move(resolution.error()));
            if (const auto* choice = std::get_if<NeedsShippingChoice>(&*resolution)) {
                return std::unexpected(error(BuildsError{.code = BuildsErrorCode::MultipleShipping,
                                                         .path = root,
                                                         .count = static_cast<u32>(choice->candidates.size())}));
            }
            return std::get<BuildLayout>(std::move(*resolution));
        },
        token,
        [&impl, id, stored, done = std::move(done)](Result<BuildLayout> layout) mutable {
            const bool cancelled = !layout && layout.error().kind == ErrorKind::Cancelled;
            const LibraryEntry* current = impl.find(id);
            if (current != nullptr && !cancelled) {
                const BuildPresence presence = layout ? BuildPresence::Present : BuildPresence::Missing;
                const auto it = impl.presence.find(id);
                const bool changed = it == impl.presence.end() || it->second != presence;
                impl.presence[id] = presence;
                // Kept only when it is rooted where the entry is, so the stored paths stay relative to it.
                const bool store = layout && normal_root(layout->root) == normal_root(current->root) &&
                                   (!stored || !(*stored == *layout));
                if (store) {
                    Result<u64> written = impl.deps.store.update([&](LibraryDocument& document) {
                        if (LibraryEntry* target = find_entry(document, id)) target->layout = to_stored_layout(*layout);
                    });
                    if (!written)
                        REBOOT_LOG_WARN(Builds, "could not store the layout of build {}", format_uuid(id.value));
                }
                if (changed) impl.publish(LibraryChange::Updated, id);
            }
            done(std::move(layout));
        });
}

}  // namespace rb::builds
