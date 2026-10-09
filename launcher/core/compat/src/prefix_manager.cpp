#include "reboot/compat/prefix_manager.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "prefix_files.hpp"
#include "reboot/compat/prefix_busy.hpp"
#include "reboot/compat/runner_env.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/process/env_builder.hpp"
#include "reboot/process/line_reader.hpp"
#include "reboot/process/wiping_launch.hpp"

namespace rb::compat {

namespace {

// How long a cancelled prefix command's tree gets to exit after it is killed.
constexpr std::chrono::seconds kKillGrace{5};
constexpr std::array<std::string_view, 3> kVcRedistArgs{"/install", "/quiet", "/norestart"};
// 1638: this or a newer version is already installed; 3010: installed, a reboot would finish it.
constexpr std::array<int, 3> kVcRedistSuccess{0, 1638, 3010};

enum class Stage : u8 { KillServer, Backup, SetAside, Boot, CheckBoot, CarrySaved, VcRuntime, Finish };

[[nodiscard]] std::string_view phase_of(Stage stage) noexcept {
    switch (stage) {
        case Stage::KillServer: return "prefix_kill_server";
        case Stage::Backup: return "prefix_backup";
        case Stage::SetAside: return "prefix_set_aside";
        case Stage::Boot:
        case Stage::CheckBoot: return "prefix_boot";
        case Stage::CarrySaved: return "prefix_carry_saved";
        case Stage::VcRuntime: return "vc_runtime";
        case Stage::Finish: return "prefix_finish";
    }
    return "prefix";
}

[[nodiscard]] Diagnostic no_prefix(RunnerKind kind) {
    return make_diag(ErrorDomain::Compat, msg::kNoPrefix).arg("runner", runner_name(kind)).kind(ErrorKind::Unsupported).build();
}

[[nodiscard]] Diagnostic cancelled(RunnerKind kind) {
    return make_diag(ErrorDomain::Compat, msg::kCancelled).arg("runner", runner_name(kind)).kind(ErrorKind::Cancelled).build();
}

[[nodiscard]] Diagnostic vc_failed(RunnerKind kind) {
    return make_diag(ErrorDomain::Compat, msg::kVcRuntimeFailed).arg("runner", runner_name(kind)).build();
}

[[nodiscard]] std::string exit_text(const ports::ChildExit& exit) {
    if (exit.code) return std::format("exit code {}", *exit.code);
    if (exit.signal) return std::format("signal {}", *exit.signal);
    return "no exit status";
}

struct Inspection {
    PrefixState state = PrefixState::Missing;
    // An earlier recreation stopped before carrying the saved data over.
    bool aside = false;
    bool needs_vc = false;
};

}  // namespace

struct PrefixManager::Impl {
    struct Job {
        u64 id = 0;
        u64 lease_id = 0;
        RunnerKind kind{};
        SessionId session;
        PrefixRequest request;
        CancelToken token;
        components::ProgressSink progress;
        UniqueFunction<void(Result<PreparedPrefix>)> done;
        NativePath dir;
        bool started = false;

        std::optional<PrefixRecord> record;
        std::deque<Stage> plan;
        bool seeded_after_boot = false;

        // The prefix command in flight.
        std::unique_ptr<ports::ChildProcess> child;
        UniqueFunction<void(Result<ports::ChildExit>)> on_exit;
        std::optional<process::LineReader> out;
        std::optional<process::LineReader> err;
        CancelRegistration cancel_registration;
        TimerHandle kill_timer;
        std::optional<components::PinnedRuntime> vc_pin;

        ~Job() {
            if (child) static_cast<void>(child->terminate_tree());
        }
    };

    Impl(PrefixManagerDeps dependencies, const AppLayout& layout)
        : deps(dependencies), prefixes_dir(layout.prefixes_dir()) {}

    ~Impl() { alive.cancel(CancelReason::Shutdown); }

    [[nodiscard]] Job* current(RunnerKind kind, u64 job_id) {
        const auto it = jobs.find(kind);
        if (it == jobs.end() || it->second.empty() || it->second.front()->id != job_id) return nullptr;
        return it->second.front().get();
    }

    // A callback that runs `then` on the strand with the job and its one argument, unless the job
    // or the manager has gone since. It may be called more than once and from any thread.
    template <class F>
    [[nodiscard]] auto on_strand(const Job& job, F then) {
        return [this, alive = alive.token(), kind = job.kind, id = job.id, then](auto value) {
            deps.strand.post([this, alive, kind, id, then, value = std::move(value)]() mutable {
                if (alive.cancelled()) return;
                if (Job* found = current(kind, id)) then(*found, std::move(value));
            });
        };
    }

    template <class T, class Work, class Then>
    void on_worker(Job& job, Work work, Then then) {
        deps.workers.submit<T>(std::move(work), job.token, deps.strand,
                               [this, alive = alive.token(), kind = job.kind, id = job.id,
                                then = std::move(then)](Result<T> result) mutable {
                                   if (alive.cancelled()) return;
                                   if (Job* found = current(kind, id)) then(*found, std::move(result));
                               });
    }

    void start_next(RunnerKind kind) {
        const auto it = jobs.find(kind);
        if (it == jobs.end() || it->second.empty() || it->second.front()->started) return;
        Job& job = *it->second.front();
        job.started = true;
        begin(job);
    }

    void finish(Job& job, Result<PreparedPrefix> result) {
        auto& queue = jobs[job.kind];
        std::unique_ptr<Job> owned = std::move(queue.front());
        queue.pop_front();
        const RunnerKind kind = owned->kind;
        owned->cancel_registration.reset();
        if (owned->done) owned->done(std::move(result));
        owned.reset();
        if (const auto it = jobs.find(kind); it != jobs.end() && it->second.empty()) jobs.erase(it);
        start_next(kind);
    }

    void fail(Job& job, Diagnostic error) { finish(job, std::unexpected(std::move(error))); }

    void report(Job& job, Stage stage) {
        if (job.progress) job.progress(Progress{.phase = phase_of(stage)});
    }

    void begin(Job& job) {
        if (!leases.contains(job.lease_id))
            return fail(job, internal_bug("PrefixManager::prepare: the lease was released"));
        if (job.token.cancelled()) return fail(job, cancelled(job.kind));
        if (job.progress) job.progress(Progress{.phase = "prefix_inspect"});

        const auto& records = deps.document.get().prefixes;
        const auto record = std::ranges::find(records, job.kind, &PrefixRecord::kind);
        if (record != records.end()) job.record = *record;
        const bool seeded = job.record && job.record->vc_runtime_seeded;

        on_worker<Inspection>(
            job,
            [&files = deps.fs, dir = job.dir, aside = aside_path(prefixes_dir, job.kind), dlls = job.request.game_dlls,
             seeded](CancelToken) -> Result<Inspection> {
                std::error_code error;
                Inspection out{inspect_prefix(dir), std::filesystem::exists(std::filesystem::symlink_status(aside, error)), false};
                if (seeded && out.state == PrefixState::Usable) return out;
                auto needs = game_needs_vc_runtime(files, dlls);
                if (!needs) return std::unexpected(std::move(needs.error()));
                out.needs_vc = *needs;
                return out;
            },
            [this](Job& found, Result<Inspection> inspection) { plan(found, std::move(inspection)); });
    }

    void plan(Job& job, Result<Inspection> inspection) {
        if (job.token.cancelled()) return fail(job, cancelled(job.kind));
        if (!inspection) return fail(job, std::move(inspection.error()));

        const bool exists = inspection->state != PrefixState::Missing;
        const bool create = inspection->state != PrefixState::Usable;
        const PrefixRequest& request = job.request;
        const std::optional<PrefixRecord>& record = job.record;
        const bool same = record && record->runtime == request.runtime && record->runtime_version == request.runtime_version;
        const bool older = record && std::is_lt(compare_runtime_versions(request.runtime_version, record->runtime_version));
        const bool upgrade = !create && !same;
        const bool seeded = !create && record && record->vc_runtime_seeded;
        const bool vc = inspection->needs_vc && !seeded;

        if (create || upgrade || vc) {
            for (const auto& [id, holder] : leases)
                if (id != job.lease_id && holder.first == job.kind)
                    return fail(job, to_diagnostic(PrefixBusy{job.kind, holder.second}));
        }

        // A wineserver of the previous runtime would refuse clients of the new one.
        if (exists && (create || upgrade)) job.plan.push_back(Stage::KillServer);
        if (upgrade && older) job.plan.push_back(Stage::Backup);
        if (exists && create) job.plan.push_back(Stage::SetAside);
        if (create || upgrade) {
            job.plan.push_back(Stage::Boot);
            job.plan.push_back(Stage::CheckBoot);
        }
        if (create && (exists || inspection->aside)) job.plan.push_back(Stage::CarrySaved);
        if (vc) job.plan.push_back(Stage::VcRuntime);
        job.plan.push_back(Stage::Finish);
        job.seeded_after_boot = seeded;
        next(job);
    }

    void next(Job& job) {
        if (job.token.cancelled()) return fail(job, cancelled(job.kind));
        const Stage stage = job.plan.front();
        job.plan.pop_front();
        report(job, stage);
        switch (stage) {
            case Stage::KillServer: return kill_server(job);
            case Stage::Backup: return back_up(job);
            case Stage::SetAside: return put_aside(job);
            case Stage::Boot: return boot(job);
            case Stage::CheckBoot: return check_boot(job);
            case Stage::CarrySaved: return carry(job);
            case Stage::VcRuntime: return fetch_vc(job);
            case Stage::Finish: return complete(job);
        }
    }

    void step_done(Job& job, Result<void> result) {
        if (job.token.cancelled()) return fail(job, cancelled(job.kind));
        if (!result) return fail(job, std::move(result.error()));
        next(job);
    }

    void kill_server(Job& job) {
        run_command(job, {ports::PrefixVerb::KillServer, {}, {}}, [this](Job& found, Result<ports::ChildExit> exit) {
            // With no server running, -k has nothing to stop; only a failed start matters.
            if (!exit) {
                Diagnostic diag = prefix_failed(found.kind, kStepKillServer);
                diag.causes.push_back(std::move(exit.error()));
                return fail(found, std::move(diag));
            }
            step_done(found, {});
        });
    }

    void back_up(Job& job) {
        on_worker<void>(
            job,
            [&files = deps.fs, dir = prefixes_dir, kind = job.kind, prefix = job.dir,
             version = job.record->runtime_version](CancelToken) {
                return back_up_prefix(files, dir, kind, prefix, version);
            },
            [this](Job& found, Result<void> result) { step_done(found, std::move(result)); });
    }

    void put_aside(Job& job) {
        on_worker<void>(
            job,
            [&files = deps.fs, kind = job.kind, prefix = job.dir, aside = aside_path(prefixes_dir, job.kind)](CancelToken) {
                return set_aside(files, kind, prefix, aside);
            },
            [this](Job& found, Result<void> result) { step_done(found, std::move(result)); });
    }

    void boot(Job& job) {
        on_worker<void>(
            job, [&files = deps.fs, dir = prefixes_dir](CancelToken) { return files.create_dirs_owner_only(dir); },
            [this](Job& found, Result<void> created) {
                if (!created) {
                    Diagnostic diag = prefix_failed(found.kind, kStepBoot);
                    diag.causes.push_back(std::move(created.error()));
                    return fail(found, std::move(diag));
                }
                run_command(found, {ports::PrefixVerb::Boot, {}, {}}, [this](Job& booted, Result<ports::ChildExit> exit) {
                    if (exit && exit->code == 0) return step_done(booted, {});
                    Diagnostic diag = prefix_failed(booted.kind, kStepBoot);
                    if (exit) diag.detail = exit_text(*exit);
                    else diag.causes.push_back(std::move(exit.error()));
                    fail(booted, std::move(diag));
                });
            });
    }

    void check_boot(Job& job) {
        on_worker<void>(
            job,
            [dir = job.dir, kind = job.kind](CancelToken) -> Result<void> {
                if (inspect_prefix(dir) != PrefixState::Usable) return std::unexpected(prefix_failed(kind, kStepBoot));
                return {};
            },
            [this](Job& found, Result<void> result) {
                if (result) record(found, found.seeded_after_boot);
                step_done(found, std::move(result));
            });
    }

    void carry(Job& job) {
        on_worker<void>(
            job,
            [&files = deps.fs, kind = job.kind, old_tree = aside_path(prefixes_dir, job.kind), prefix = job.dir](CancelToken) {
                return carry_saved(files, kind, old_tree, prefix);
            },
            [this](Job& found, Result<void> result) { step_done(found, std::move(result)); });
    }

    void fetch_vc(Job& job) {
        if (!job.request.vc_redist) return fail(job, internal_bug("PrefixManager::prepare: no VC++ source"));
        VcRedistSource source = std::move(job.request.vc_redist);
        source(on_strand(job, [this](Job& found, Result<components::PinnedRuntime> pinned) {
            if (found.token.cancelled()) return fail(found, cancelled(found.kind));
            if (!pinned) {
                Diagnostic diag = vc_failed(found.kind);
                diag.causes.push_back(std::move(pinned.error()));
                return fail(found, std::move(diag));
            }
            found.vc_pin = std::move(*pinned);
            ports::PrefixCommand install{ports::PrefixVerb::Run, found.vc_pin->runtime.root / kVcRedistInstaller, {}};
            install.args.assign(kVcRedistArgs.begin(), kVcRedistArgs.end());
            run_command(found, std::move(install), [this](Job& installed, Result<ports::ChildExit> exit) {
                installed.vc_pin.reset();
                const bool ok = exit && exit->code && std::ranges::find(kVcRedistSuccess, *exit->code) != kVcRedistSuccess.end();
                if (!ok) {
                    Diagnostic diag = vc_failed(installed.kind);
                    if (exit) diag.detail = exit_text(*exit);
                    else diag.causes.push_back(std::move(exit.error()));
                    return fail(installed, std::move(diag));
                }
                record(installed, true);
                step_done(installed, {});
            });
        }));
    }

    void complete(Job& job) {
        on_worker<PreparedPrefix>(
            job,
            [&files = deps.fs, kind = job.kind, dir = job.dir](CancelToken) -> Result<PreparedPrefix> {
                if (kind == RunnerKind::MacRuntime)
                    if (auto seeded = seed_rhi(files, kind, dir); !seeded) return std::unexpected(std::move(seeded.error()));
                auto paths = PathMapper::load(dir);
                if (!paths) return std::unexpected(std::move(paths.error()));
                return PreparedPrefix{dir, std::move(*paths)};
            },
            [this](Job& found, Result<PreparedPrefix> prepared) {
                if (found.token.cancelled()) return fail(found, cancelled(found.kind));
                finish(found, std::move(prepared));
            });
    }

    // What the prefix now holds, so the next prepare starts from it even if a later step fails.
    void record(Job& job, bool vc_seeded) {
        PrefixRecord next{job.kind, job.request.runtime, job.request.runtime_version, vc_seeded};
        const auto written = deps.document.update([&next](CompatDocument& document) {
            const auto it = std::ranges::find(document.prefixes, next.kind, &PrefixRecord::kind);
            if (it != document.prefixes.end()) *it = next;
            else document.prefixes.push_back(next);
        });
        if (!written)
            REBOOT_LOG_WARN(Play, "the record of the {} prefix was not saved: {}", runner_name(job.kind), written.error().id);
        job.record = std::move(next);
    }

    void run_command(Job& job, ports::PrefixCommand command, UniqueFunction<void(Job&, Result<ports::ChildExit>)> then) {
        process::EnvBuilder builder(process::EnvSyntax::Posix);
        builder.daemon_base(job.request.env).runner(runner_layer(job.kind, job.request.layout));
        Result<process::BuiltEnv> env = std::move(builder).build();
        if (!env) return then(job, std::unexpected(std::move(env.error())));
        auto launch = deps.runner.prefix_command(job.request.layout, job.dir, command, env->copy());
        if (!launch) return then(job, std::unexpected(std::move(launch.error())));
        const process::WipingLaunch wiping(std::move(*launch));
        auto child = deps.processes.spawn(wiping.get());
        if (!child) return then(job, std::unexpected(std::move(child.error())));

        job.child = std::move(*child);
        job.on_exit = [this, then = std::move(then), kind = job.kind, id = job.id](Result<ports::ChildExit> exit) mutable {
            Job* found = current(kind, id);
            if (found == nullptr) return;
            if (found->token.cancelled()) return fail(*found, cancelled(kind));
            then(*found, std::move(exit));
        };
        const SessionId session = job.session;
        const auto log_line = [session](std::string_view line, bool) {
            REBOOT_LOG_AT(LogLevel::Debug, Wine, session, "{}", line);
        };
        job.out.emplace(log_line);
        job.err.emplace(log_line);
        // The adapter's bytes are borrowed for the call only.
        job.child->on_stdout([post = on_strand(job, [](Job& found, std::vector<u8> bytes) {
                                  if (found.out) found.out->feed(bytes);
                              })](std::span<const u8> bytes) { post(std::vector<u8>(bytes.begin(), bytes.end())); });
        job.child->on_stderr([post = on_strand(job, [](Job& found, std::vector<u8> bytes) {
                                  if (found.err) found.err->feed(bytes);
                              })](std::span<const u8> bytes) { post(std::vector<u8>(bytes.begin(), bytes.end())); });
        job.child->on_exit(on_strand(job, [](Job& found, ports::ChildExit exit) { command_exited(found, exit); }));
        job.cancel_registration = job.token.on_cancel(on_strand(job, [this](Job& found, CancelReason) { kill(found); }));
    }

    static void command_exited(Job& job, ports::ChildExit exit) {
        if (job.out) job.out->finish();
        if (job.err) job.err->finish();
        job.out.reset();
        job.err.reset();
        job.child.reset();
        job.kill_timer.cancel();
        job.cancel_registration.reset();
        if (auto then = std::move(job.on_exit)) then(exit);
    }

    // The tree gets kKillGrace to report its exit; the job ends cancelled either way.
    void kill(Job& job) {
        if (!job.child) return;
        static_cast<void>(job.child->terminate_tree());
        job.kill_timer = deps.timers.after(kKillGrace, [this, alive = alive.token(), kind = job.kind, id = job.id] {
            if (alive.cancelled()) return;
            if (Job* found = current(kind, id)) fail(*found, cancelled(kind));
        });
    }

    PrefixManagerDeps deps;
    NativePath prefixes_dir;
    CancelSource alive;
    u64 next_lease = 1;
    u64 next_job = 1;
    std::map<u64, std::pair<RunnerKind, SessionId>> leases;
    std::map<RunnerKind, std::deque<std::unique_ptr<Job>>> jobs;
};

PrefixManager::PrefixManager(PrefixManagerDeps deps, const AppLayout& layout)
    : impl_(std::make_unique<Impl>(deps, layout)) {}

PrefixManager::~PrefixManager() = default;

Result<NativePath> PrefixManager::prefix_dir(RunnerKind kind) const {
    if (!wine_runtime_kind(kind)) return std::unexpected(no_prefix(kind));
    return impl_->prefixes_dir / runner_name(kind);
}

Result<PrefixLease> PrefixManager::lease(RunnerKind kind, SessionId session) {
    if (!wine_runtime_kind(kind)) return std::unexpected(no_prefix(kind));
    const u64 id = impl_->next_lease++;
    impl_->leases.emplace(id, std::pair{kind, session});
    return PrefixLease(*this, id, kind, session);
}

bool PrefixManager::idle(RunnerKind kind) const {
    if (impl_->jobs.contains(kind)) return false;
    return std::ranges::none_of(impl_->leases, [kind](const auto& entry) { return entry.second.first == kind; });
}

void PrefixManager::prepare(const PrefixLease& lease, PrefixRequest request, CancelToken token,
                            components::ProgressSink progress, UniqueFunction<void(Result<PreparedPrefix>)> done) {
    if (lease.manager_ != this || !impl_->leases.contains(lease.id_)) {
        impl_->deps.strand.post([done = std::move(done), alive = impl_->alive.token()]() mutable {
            if (!alive.cancelled()) done(std::unexpected(internal_bug("PrefixManager::prepare: not a lease of this manager")));
        });
        return;
    }
    auto job = std::make_unique<Impl::Job>();
    job->id = impl_->next_job++;
    job->lease_id = lease.id_;
    job->kind = lease.kind();
    job->session = lease.session();
    job->request = std::move(request);
    job->token = std::move(token);
    job->progress = std::move(progress);
    job->done = std::move(done);
    job->dir = impl_->prefixes_dir / runner_name(job->kind);
    const RunnerKind kind = job->kind;
    impl_->jobs[kind].push_back(std::move(job));
    // `done` never runs inside prepare().
    impl_->deps.strand.post([impl = impl_.get(), alive = impl_->alive.token(), kind] {
        if (!alive.cancelled()) impl->start_next(kind);
    });
}

void PrefixManager::release(u64 lease_id) noexcept { impl_->leases.erase(lease_id); }

}  // namespace rb::compat
