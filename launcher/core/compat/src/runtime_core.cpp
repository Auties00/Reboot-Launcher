#include "runtime_core.hpp"

#include <algorithm>
#include <any>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "messages.hpp"
#include "reboot/compat/prefix_manager.hpp"
#include "reboot/compat/rosetta_install_answer.hpp"
#include "reboot/compat/rosetta_install_request.hpp"
#include "reboot/compat/runtime_id.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/ports/runner.hpp"

namespace rb::compat {

namespace {

// SlrSetup reports no progress while it downloads the runtime, so the liveness bound is wide.
constexpr std::chrono::minutes kSetupDeadline{30};

[[nodiscard]] Diagnostic runner_diag(MessageId message, RunnerKind kind, ErrorKind error_kind) {
    return make_diag(ErrorDomain::Compat, message).arg("runner", runner_name(kind)).kind(error_kind).build();
}

[[nodiscard]] Diagnostic cancelled(RunnerKind kind) { return runner_diag(msg::kCancelled, kind, ErrorKind::Cancelled); }

// The newest entry of `kind`; equal versions go to the greater id, so the order of the manifest never matters.
[[nodiscard]] std::optional<components::RuntimeEntry> select(const std::vector<components::RuntimeEntry>& entries,
                                                             components::RuntimeKind kind) {
    std::optional<components::RuntimeEntry> best;
    for (const components::RuntimeEntry& entry : entries) {
        if (entry.kind != kind) continue;
        if (best) {
            const std::strong_ordering order = compare_runtime_versions(entry.version, best->version);
            if (std::is_lt(order) || (std::is_eq(order) && entry.id <= best->id)) continue;
        }
        best = entry;
    }
    return best;
}

[[nodiscard]] RuntimeRecord& runtime_record(CompatDocument& document, const RuntimeId& runtime) {
    const auto it = std::ranges::find(document.runtimes, runtime, &RuntimeRecord::runtime);
    if (it != document.runtimes.end()) return *it;
    return document.runtimes.emplace_back(RuntimeRecord{runtime, false, std::nullopt});
}

}  // namespace

struct RuntimeCore::Impl {
    struct Prepare {
        u64 id = 0;
        SessionId session;
        RunnerProfile profile;
        OperationBase* op = nullptr;
        components::ProgressSink progress;
        UniqueFunction<void(Result<PreparedRuntime>)> done;
        PrefixLease lease;
        std::optional<components::PinnedRuntime> wine;
        std::optional<components::PinnedRuntime> launcher;
        CancelRegistration cancel_registration;
    };

    struct Setup {
        RunnerProfile profile;
        Operation<std::vector<components::ComponentRef>>* op = nullptr;
        std::optional<components::PinnedRuntime> wine;
        std::optional<components::PinnedRuntime> launcher;
    };

    explicit Impl(RuntimeCoreDeps dependencies) : deps(dependencies) {}
    ~Impl() { alive.cancel(CancelReason::Shutdown); }

    [[nodiscard]] bool is_supported(RunnerKind kind) const {
        const std::vector<RunnerKind> kinds = deps.runner.supported();
        return std::ranges::find(kinds, kind) != kinds.end();
    }

    [[nodiscard]] const RuntimeRecord* find_record(const RuntimeId& runtime) const {
        const auto& records = deps.document.get().runtimes;
        const auto it = std::ranges::find(records, runtime, &RuntimeRecord::runtime);
        return it == records.end() ? nullptr : &*it;
    }

    void update_record(const RuntimeId& runtime, UniqueFunction<void(RuntimeRecord&)> change) {
        const auto written = deps.document.update(
            [&](CompatDocument& document) { change(runtime_record(document, runtime)); });
        if (!written)
            REBOOT_LOG_WARN(Play, "the record of runtime {} was not saved: {}", runtime.value, written.error().id);
    }

    // Posts `then` with the prepare it belongs to, unless the prepare or the service has ended.
    template <class F>
    void post(u64 id, F then) {
        deps.strand.post([this, alive_token = alive.token(), id, then = std::move(then)]() mutable {
            if (alive_token.cancelled()) return;
            if (Prepare* found = find(id)) then(*found);
        });
    }

    [[nodiscard]] Prepare* find(u64 id) {
        const auto it = prepares.find(id);
        return it == prepares.end() ? nullptr : it->second.get();
    }

    void finish(Prepare& prepare, Result<PreparedRuntime> result) {
        const auto it = prepares.find(prepare.id);
        std::unique_ptr<Prepare> owned = std::move(it->second);
        prepares.erase(it);
        owned->cancel_registration.reset();
        if (owned->done) owned->done(std::move(result));
    }

    void fail(Prepare& prepare, Diagnostic error) {
        if (prepare.op->token().cancelled()) error = cancelled(prepare.profile.kind);
        finish(prepare, std::unexpected(std::move(error)));
    }

    void check_rosetta(Prepare& prepare) {
        deps.workers.submit<std::optional<UserRequestKind>>(
            [&runner = deps.runner](CancelToken) -> Result<std::optional<UserRequestKind>> {
                return runner.pending_prerequisite();
            },
            prepare.op->token(), deps.strand,
            [this, alive_token = alive.token(), id = prepare.id](Result<std::optional<UserRequestKind>> pending) {
                if (alive_token.cancelled()) return;
                Prepare* found = find(id);
                if (found == nullptr) return;
                if (!pending) return fail(*found, std::move(pending.error()));
                if (*pending == UserRequestKind::RosettaInstall) return ask_rosetta(*found);
                after_rosetta(*found);
            });
    }

    void ask_rosetta(Prepare& prepare) {
        const RequestId request = deps.requests.ask(
            UserRequestKind::RosettaInstall, RosettaInstallRequest{}, prepare.op->id(), prepare.session,
            [this, alive_token = alive.token(), id = prepare.id](const std::any& answer) -> Result<void> {
                if (alive_token.cancelled()) return {};
                const auto* decision = std::any_cast<RosettaInstallAnswer>(&answer);
                if (decision == nullptr)
                    return make_diag(ErrorDomain::Compat, msg::kAnswerInvalid).kind(ErrorKind::InvalidInput).fail();
                // respond() needs the verdict now, and the check is a single stat.
                if (*decision == RosettaInstallAnswer::Installed &&
                    deps.runner.pending_prerequisite() == UserRequestKind::RosettaInstall)
                    return make_diag(ErrorDomain::Compat, msg::kRosettaMissing).kind(ErrorKind::Conflict).fail();
                const bool declined = *decision == RosettaInstallAnswer::Declined;
                post(id, [this, declined](Prepare& found) {
                    if (declined)
                        return fail(found, runner_diag(msg::kRosettaDeclined, found.profile.kind, ErrorKind::Cancelled));
                    found.op->progress(Progress{.phase = "runtime"});
                    after_rosetta(found);
                });
                return {};
            },
            prepare.op->token());
        prepare.op->awaiting_user(request);
    }

    void after_rosetta(Prepare& prepare) {
        if (prepare.profile.kind == RunnerKind::Umu) {
            const RuntimeRecord* record = prepare.profile.launcher ? find_record(*prepare.profile.launcher) : nullptr;
            if (record == nullptr || !record->slr)
                return fail(prepare, runner_diag(msg::kRuntimeSetupRequired, RunnerKind::Umu, ErrorKind::Conflict));
        }
        acquire(prepare, prepare.profile.runtime, [this](Prepare& found, components::PinnedRuntime wine) {
            found.wine = std::move(wine);
            if (!found.profile.launcher) return resolve(found);
            acquire(found, *found.profile.launcher, [this](Prepare& with, components::PinnedRuntime launcher) {
                with.launcher = std::move(launcher);
                resolve(with);
            });
        });
    }

    template <class Then>
    void acquire(Prepare& prepare, const RuntimeId& runtime, Then then) {
        deps.catalog.acquire(
            prepare.session, runtime.value, prepare.op->token(),
            [this, alive_token = alive.token(), id = prepare.id](const Progress& progress) {
                if (alive_token.cancelled()) return;
                if (Prepare* found = find(id); found != nullptr && found->progress) found->progress(progress);
            },
            [this, alive_token = alive.token(), id = prepare.id, then = std::move(then)](
                Result<components::PinnedRuntime> pinned) mutable {
                if (alive_token.cancelled()) return;
                Prepare* found = find(id);
                if (found == nullptr) return;
                if (!pinned) return fail(*found, std::move(pinned.error()));
                then(*found, std::move(*pinned));
            });
    }

    // post_extract on every pinned runtime, since the store may have re-extracted one under its id,
    // then the layout from all of them at once.
    void resolve(Prepare& prepare) {
        ports::RuntimeDirs dirs{prepare.wine->runtime.root, std::nullopt};
        if (prepare.launcher) dirs.launcher = prepare.launcher->runtime.root;
        deps.workers.submit<ports::RuntimeLayout>(
            [&runner = deps.runner, kind = prepare.profile.kind, dirs](CancelToken) -> Result<ports::RuntimeLayout> {
                if (auto extracted = runner.post_extract(dirs.runtime); !extracted) return std::unexpected(extracted.error());
                if (dirs.launcher)
                    if (auto extracted = runner.post_extract(*dirs.launcher); !extracted)
                        return std::unexpected(extracted.error());
                return runner.layout(kind, dirs);
            },
            prepare.op->token(), deps.strand,
            [this, alive_token = alive.token(), id = prepare.id](Result<ports::RuntimeLayout> layout) {
                if (alive_token.cancelled()) return;
                Prepare* found = find(id);
                if (found == nullptr) return;
                if (!layout) return fail(*found, std::move(layout.error()));
                PreparedRuntime prepared{found->profile, std::move(*layout), std::move(*found->wine),
                                         std::move(found->launcher), std::move(found->lease)};
                finish(*found, std::move(prepared));
            });
    }

    void setup_failed(RunnerKind kind, Diagnostic error) {
        const auto it = setups.find(kind);
        std::unique_ptr<Setup> setup = std::move(it->second);
        setups.erase(it);
        const CancelToken token = setup->op->token();
        if (token.cancelled()) {
            setup->op->complete(Cancelled{token.reason().value_or(CancelReason::User)});
            return;
        }
        setup->op->complete(Failed{make_diag(ErrorDomain::Compat, msg::kRuntimeSetupFailed)
                                       .arg("runner", runner_name(kind))
                                       .cause(std::move(error))
                                       .build()});
    }

    void setup_acquire(RunnerKind kind, const RuntimeId& runtime, UniqueFunction<void(Setup&, components::PinnedRuntime)> then) {
        Setup& setup = *setups.at(kind);
        deps.catalog.acquire(
            SessionId{}, runtime.value, setup.op->token(),
            [this, alive_token = alive.token(), kind](const Progress& progress) {
                if (!alive_token.cancelled() && setups.contains(kind)) setups.at(kind)->op->progress(progress);
            },
            [this, alive_token = alive.token(), kind, then = std::move(then)](Result<components::PinnedRuntime> pinned) mutable {
                if (alive_token.cancelled() || !setups.contains(kind)) return;
                if (!pinned) return setup_failed(kind, std::move(pinned.error()));
                then(*setups.at(kind), std::move(*pinned));
            });
    }

    void run_setup(RunnerKind kind) {
        Setup& setup = *setups.at(kind);
        setup_acquire(kind, setup.profile.runtime, [this, kind](Setup& with_wine, components::PinnedRuntime wine) {
            with_wine.wine = std::move(wine);
            if (!with_wine.profile.launcher) return setup_runner(kind);
            setup_acquire(kind, *with_wine.profile.launcher, [this, kind](Setup& with, components::PinnedRuntime launcher) {
                with.launcher = std::move(launcher);
                setup_runner(kind);
            });
        });
    }

    void setup_runner(RunnerKind kind) {
        Setup& setup = *setups.at(kind);
        setup.op->progress(Progress{.phase = "runtime_setup"});
        ports::RuntimeDirs dirs{setup.wine->runtime.root, std::nullopt};
        if (setup.launcher) dirs.launcher = setup.launcher->runtime.root;
        deps.workers.submit<std::optional<std::string>>(
            [&runner = deps.runner, kind, dirs](CancelToken token) -> Result<std::optional<std::string>> {
                if (auto extracted = runner.post_extract(dirs.runtime); !extracted) return std::unexpected(extracted.error());
                if (dirs.launcher)
                    if (auto extracted = runner.post_extract(*dirs.launcher); !extracted)
                        return std::unexpected(extracted.error());
                auto layout = runner.layout(kind, dirs);
                if (!layout) return std::unexpected(std::move(layout.error()));
                if (kind != RunnerKind::Umu) return std::nullopt;
                return runner.runtime_setup(*layout, std::move(token));
            },
            setup.op->token(), deps.strand,
            [this, alive_token = alive.token(), kind](Result<std::optional<std::string>> build) {
                if (alive_token.cancelled() || !setups.contains(kind)) return;
                if (!build) return setup_failed(kind, std::move(build.error()));
                const auto it = setups.find(kind);
                std::unique_ptr<Setup> done = std::move(it->second);
                setups.erase(it);
                if (*build && done->profile.launcher) {
                    SlrInstall installed{std::move(**build), deps.clock.system_now()};
                    update_record(*done->profile.launcher,
                                  [&installed](RuntimeRecord& record) { record.slr = std::move(installed); });
                }
                std::vector<components::ComponentRef> ensured{done->wine->runtime.ref};
                if (done->launcher) ensured.push_back(done->launcher->runtime.ref);
                done->op->complete(Completed<std::vector<components::ComponentRef>>{std::move(ensured)});
            });
    }

    RuntimeCoreDeps deps;
    CancelSource alive;
    u64 next_prepare = 1;
    std::map<u64, std::unique_ptr<Prepare>> prepares;
    std::map<RunnerKind, std::unique_ptr<Setup>> setups;
};

RuntimeCore::RuntimeCore(RuntimeCoreDeps deps) : impl_(std::make_unique<Impl>(deps)) {}

RuntimeCore::~RuntimeCore() = default;

std::vector<RunnerKind> RuntimeCore::supported() const { return impl_->deps.runner.supported(); }

Result<RunnerProfile> RuntimeCore::profile(RunnerKind kind) const {
    const auto wine_kind = wine_runtime_kind(kind);
    if (!wine_kind || !impl_->is_supported(kind))
        return std::unexpected(runner_diag(msg::kRunnerUnsupported, kind, ErrorKind::Unsupported));
    const std::vector<components::RuntimeEntry> entries = impl_->deps.catalog.runtimes();
    const auto wine = select(entries, *wine_kind);
    if (!wine) return std::unexpected(runner_diag(msg::kNoRuntime, kind, ErrorKind::NotFound));

    RunnerProfile out;
    out.kind = kind;
    out.runtime = RuntimeId{wine->id};
    if (kind == RunnerKind::Umu) {
        const auto launcher = select(entries, components::RuntimeKind::Umu);
        if (!launcher) return std::unexpected(runner_diag(msg::kNoRuntime, kind, ErrorKind::NotFound));
        out.launcher = RuntimeId{launcher->id};
    }
    if (kind == RunnerKind::MacRuntime) {
        const RuntimeRecord* record = impl_->find_record(out.runtime);
        out.rosetta_cold = record == nullptr || !record->completed_session;
    }
    return out;
}

void RuntimeCore::prepare(SessionId session, const RunnerProfile& profile, OperationBase& op,
                          components::ProgressSink progress, UniqueFunction<void(Result<PreparedRuntime>)> done) {
    Impl& impl = *impl_;
    const auto refuse = [&](Diagnostic error) {
        impl.deps.strand.post([alive = impl.alive.token(), done = std::move(done), error = std::move(error)]() mutable {
            if (!alive.cancelled()) done(std::unexpected(std::move(error)));
        });
    };
    if (impl.setups.contains(profile.kind))
        return refuse(runner_diag(msg::kRuntimeSetupRunning, profile.kind, ErrorKind::Conflict));
    Result<PrefixLease> lease = impl.deps.prefixes.lease(profile.kind, session);
    if (!lease) return refuse(std::move(lease.error()));

    auto state = std::make_unique<Impl::Prepare>();
    state->id = impl.next_prepare++;
    state->session = session;
    state->profile = profile;
    state->op = &op;
    state->progress = std::move(progress);
    state->done = std::move(done);
    state->lease = std::move(*lease);
    Impl::Prepare& prepare = *state;
    impl.prepares.emplace(prepare.id, std::move(state));

    // A withdrawn Rosetta request tells nobody, so the cancel itself ends the prepare.
    prepare.cancel_registration = op.token().on_cancel([&impl, id = prepare.id](CancelReason) {
        impl.post(id, [&impl](Impl::Prepare& found) { impl.fail(found, cancelled(found.profile.kind)); });
    });
    if (profile.kind == RunnerKind::MacRuntime) return impl.check_rosetta(prepare);
    impl.post(prepare.id, [&impl](Impl::Prepare& found) { impl.after_rosetta(found); });
}

VcRedistSource RuntimeCore::vc_redist_source(SessionId session, CancelToken token) {
    return [&impl = *impl_, alive = impl_->alive.token(), session,
            token](UniqueFunction<void(Result<components::PinnedRuntime>)> done) mutable {
        if (alive.cancelled()) return;
        const auto entry = select(impl.deps.catalog.runtimes(), components::RuntimeKind::VcRedist);
        if (!entry)
            return done(std::unexpected(
                make_diag(ErrorDomain::Compat, msg::kNoVcRedist).kind(ErrorKind::NotFound).build()));
        impl.deps.catalog.acquire(session, entry->id, token, nullptr, std::move(done));
    };
}

Result<OpHandle> RuntimeCore::start_setup(RunnerKind kind, DisconnectPolicy policy) {
    Impl& impl = *impl_;
    if (!wine_runtime_kind(kind) || !impl.is_supported(kind))
        return std::unexpected(runner_diag(msg::kRunnerUnsupported, kind, ErrorKind::Unsupported));
    if (impl.setups.contains(kind))
        return std::unexpected(runner_diag(msg::kRuntimeSetupRunning, kind, ErrorKind::Conflict));
    if (!impl.deps.prefixes.idle(kind)) return std::unexpected(runner_diag(msg::kRuntimeInUse, kind, ErrorKind::Conflict));
    Result<RunnerProfile> selected = profile(kind);
    if (!selected) return std::unexpected(std::move(selected.error()));

    auto [handle, op] = impl.deps.ops.create<std::vector<components::ComponentRef>>(
        OpKind::RuntimeSetup, policy, std::nullopt, RunnerMultiplier::Native, kSetupDeadline);
    auto setup = std::make_unique<Impl::Setup>();
    setup->profile = std::move(*selected);
    setup->op = &op;
    impl.setups.emplace(kind, std::move(setup));
    impl.run_setup(kind);
    return handle;
}

void RuntimeCore::mark_good(const PreparedRuntime& runtime) {
    impl_->deps.catalog.mark_good(runtime.wine.pin);
    if (runtime.launcher) impl_->deps.catalog.mark_good(runtime.launcher->pin);
    impl_->update_record(runtime.profile.runtime, [](RuntimeRecord& record) { record.completed_session = true; });
}

}  // namespace rb::compat
