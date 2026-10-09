#include "reboot/updates/update_service.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <cstddef>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/components/manifest_platform.hpp"
#include "reboot/components/manifest_service.hpp"
#include "reboot/components/release_manifest.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/net/resumable_downloader.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/updates/activity_probe.hpp"
#include "reboot/updates/apply_gate.hpp"
#include "reboot/updates/update_event.hpp"
#include "reboot/updates/update_prompts.hpp"

namespace rb::updates {

namespace {

using components::AppEntry;
using components::RemoteFile;

[[nodiscard]] DiagBuilder updates_diag(MessageId message) { return make_diag(ErrorDomain::Updates, message); }

[[nodiscard]] Diagnostic busy() {
    return updates_diag(msg::kBusy).kind(ErrorKind::Conflict).retryable().build();
}

[[nodiscard]] bool safe_file_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
           c == '_' || c == '+';
}

// The URL's last path segment: Velopack only applies a package under its feed's file name.
[[nodiscard]] std::string package_file_name(const RemoteFile& package, const SemVer& version) {
    std::string_view url = package.urls.front();
    url = url.substr(0, url.find_first_of("?#"));
    const std::size_t slash = url.rfind('/');
    const std::string_view name = slash == std::string_view::npos ? url : url.substr(slash + 1);
    if (name.empty() || name == "." || name == ".." || !std::ranges::all_of(name, safe_file_char))
        return "reboot-launcher-" + version.to_string();
    return std::string(name);
}

// The downloader writes a plain file, so it is hashed the same way.
[[nodiscard]] Result<void> verify_package(ports::IFileSystem& fs, const NativePath& file, const RemoteFile& package,
                                          const SemVer& version, const CancelToken& token) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return updates_diag(msg::kDownloadFailed).arg("version", version).detail("the download is not readable").fail();
    Sha256 hasher;
    std::array<char, 64 * 1024> buffer{};
    u64 size = 0;
    while (in && size <= package.size && !token.cancelled()) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        hasher.update(std::span<const u8>(reinterpret_cast<const u8*>(buffer.data()), got));
        size += got;
    }
    if (token.cancelled()) return updates_diag(msg::kDownloadFailed).arg("version", version).kind(ErrorKind::Cancelled).fail();
    if (in.bad())
        return updates_diag(msg::kDownloadFailed).arg("version", version).detail("reading the download failed").fail();
    in.close();
    if (size == package.size && hasher.finish() == package.sha256) return {};
    // A later attempt must not resume onto bytes that never matched.
    if (auto removed = fs.remove_tree(file); !removed)
        REBOOT_LOG_WARN(Update, "Cannot remove the mismatched update download: {}", removed.error().id);
    return updates_diag(msg::kChecksumMismatch).arg("version", version).retryable().fail();
}

}  // namespace

struct UpdateService::Impl {
    struct CheckJob {
        Operation<std::optional<UpdateOffer>>* op = nullptr;
        OpHandle handle;
        CheckTrigger trigger{};
    };

    struct ApplyJob {
        Operation<ApplyStatus>* op = nullptr;
        OpHandle handle;
        ApplyWhen when{};
        AppEntry entry;
        CancelRegistration on_cancel;
    };

    // A pending ConfirmStopSessions; cancelling `withdraw` withdraws it.
    struct Prompt {
        u64 serial = 0;
        CancelSource withdraw;
    };

    Impl(UpdateServiceDeps service_deps, UpdateOptions service_options, const AppLayout& layout)
        : deps(std::move(service_deps)),
          options(std::move(service_options)),
          marker_path(layout.update_marker()),
          download_root(layout.manifest_cache().parent_path() / "updates"),
          mode(deps.applier.supports_in_place() ? UpdateMode::InPlace : UpdateMode::NotifyOnly),
          channel(options.channel),
          auto_check(options.auto_check) {
        deps.activity.set_on_change([this] { on_activity_changed(); });
    }

    ~Impl() {
        alive.cancel(CancelReason::Shutdown);
        deps.activity.set_on_change(nullptr);
        withdraw_prompt(CancelReason::Shutdown);
        if (check) shut_down(*check->op);
        if (apply_job) {
            apply_job->on_cancel.reset();
            shut_down(*apply_job->op);
        }
    }

    // The op's work never finishes now; completing it lets the registry free it.
    template <class T>
    void shut_down(Operation<T>& op) {
        (void)deps.ops.cancel(op.id(), CancelReason::Shutdown);
        (void)op.complete(Cancelled{CancelReason::Shutdown});
    }

    // Drops a callback that arrives after the service is gone.
    template <class F>
    [[nodiscard]] auto guarded(F callback) const {
        return [alive = alive.token(), callback = std::move(callback)]<class... A>(A&&... args) mutable {
            if (!alive.cancelled()) callback(std::forward<A>(args)...);
        };
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    Result<StartupResume> begin_startup() {
        StartupResume out;
        Result<std::vector<u8>> bytes = deps.fs.read_all(marker_path);
        if (!bytes && bytes.error().kind != ErrorKind::NotFound) return std::unexpected(std::move(bytes.error()));
        if (bytes) {
            Result<PendingUpdateMarker> parsed = parse_marker(*bytes);
            if (!parsed) {
                REBOOT_LOG_WARN(Update, "Removing a malformed update marker ({})", parsed.error().id);
                out.verdict = MarkerVerdict::Foreign;
                remove_marker_now();
            } else {
                PendingUpdateMarker marker = std::move(*parsed);
                if (marker.to == options.installed && !options.shim_counts_attempts) {
                    ++marker.attempts;
                    marker.started_at = deps.clock.system_now();
                    if (auto counted = deps.fs.atomic_replace(marker_path, encode_marker(marker), false); !counted)
                        return std::unexpected(std::move(counted.error()));
                }
                out.verdict = judge_marker(marker, options.installed);
                out.marker = marker;
                judged(*out.verdict, marker);
            }
        }
        ResumeRecord record = resume_record_from(deps.resume.get());
        if (record != ResumeRecord{}) out.resume = std::move(record);
        // A start that still has to pass its self-test keeps resume.json for the next attempt.
        if (!unconfirmed) clear_resume();
        return out;
    }

    void judged(MarkerVerdict verdict, const PendingUpdateMarker& marker) {
        switch (verdict) {
            case MarkerVerdict::SelfTest: unconfirmed = marker; return;
            case MarkerVerdict::NotApplied:
                REBOOT_LOG_WARN(Update, "Launcher {} was not installed; {} is still running", marker.to.to_string(),
                                options.installed.to_string());
                // Not retried on its own, so a failing apply cannot loop through restarts.
                not_applied = marker.to;
                startup_failure(UpdateStage::Apply, updates_diag(msg::kNotApplied)
                                                        .arg("version", marker.to)
                                                        .arg("installed", options.installed)
                                                        .build());
                break;
            case MarkerVerdict::GiveUp:
                REBOOT_LOG_ERROR(Update, "Launcher {} failed its startup check {} times; running it anyway",
                                 marker.to.to_string(), marker.attempts);
                startup_failure(UpdateStage::Confirm,
                                updates_diag(msg::kGaveUp).arg("version", marker.to).arg("attempts", marker.attempts).build());
                break;
            case MarkerVerdict::Foreign:
                REBOOT_LOG_WARN(Update, "Removing an update marker for {} -> {}, neither of which is running",
                                marker.from.to_string(), marker.to.to_string());
                break;
        }
        remove_marker_now();
    }

    void startup_failure(UpdateStage stage, Diagnostic error) {
        last_error = error;
        phase = UpdatePhase::Failed;
        pending_failure = UpdateFailed{stage, std::move(error)};
    }

    void remove_marker_now() {
        if (auto removed = deps.fs.remove_tree(marker_path); !removed)
            REBOOT_LOG_WARN(Update, "Cannot remove the update marker: {}", removed.error().id);
    }

    Result<void> confirm_startup(Result<void> self_test) {
        if (!unconfirmed) return {};
        if (!self_test) {
            Diagnostic failure = updates_diag(msg::kSelfTestFailed)
                                     .arg("version", unconfirmed->to)
                                     .arg("attempt", unconfirmed->attempts)
                                     .arg("max_attempts", kMaxUpdateAttempts)
                                     .cause(std::move(self_test.error()))
                                     .build();
            last_error = failure;
            return std::unexpected(std::move(failure));
        }
        unconfirmed.reset();
        clear_resume();
        deps.workers.submit<void>(
            [&fs = deps.fs, marker = marker_path, downloads = download_root](CancelToken) -> Result<void> {
                // The package was consumed by the apply that brought this version in.
                if (auto removed = fs.remove_tree(downloads); !removed)
                    REBOOT_LOG_WARN(Update, "Cannot remove old update downloads: {}", removed.error().id);
                return fs.remove_tree(marker);
            },
            CancelToken{}, deps.strand, [](Result<void> removed) {
                if (!removed) REBOOT_LOG_WARN(Update, "Cannot remove the confirmed update marker: {}", removed.error().id);
            });
        return {};
    }

    void clear_resume() {
        if (resume_record_from(deps.resume.get()) == ResumeRecord{} && !deps.resume.get().pending_update) return;
        if (auto cleared = deps.resume.update([](storage::ResumeDocument& document) { document = {}; }); !cleared)
            REBOOT_LOG_WARN(Update, "Cannot clear state/resume.json: {}", cleared.error().id);
    }

    void start() {
        if (pending_failure) publish(deps.events, *std::exchange(pending_failure, std::nullopt));
        if (auto_check) (void)run_check(CheckTrigger::Startup, DisconnectPolicy::BoundToConnection);
    }

    [[nodiscard]] bool apply_active() const noexcept {
        return apply_job.has_value() || gate.armed() || phase == UpdatePhase::Applying;
    }

    [[nodiscard]] UpdateState state() const {
        UpdateState out;
        out.installed = options.installed;
        out.mode = mode;
        out.channel = channel;
        out.phase = phase;
        out.offer = offer;
        out.last_check = deps.state.get().last_update_check;
        out.last_error = last_error;
        if (phase == UpdatePhase::Staged || phase == UpdatePhase::WaitingForIdle || phase == UpdatePhase::Draining)
            out.blocking = gate.blocking();
        return out;
    }

    void persist_state(UniqueFunction<void(storage::StateDocument&)> mutate) {
        if (auto stored = deps.state.update(std::move(mutate)); !stored)
            REBOOT_LOG_WARN(Update, "Cannot record update state in state.json: {}", stored.error().id);
    }

    [[nodiscard]] std::optional<UpdateOffer> select_current() const {
        const components::ReleaseManifest* manifest = deps.manifest.current();
        if (manifest == nullptr) return std::nullopt;
        return select_offer(*manifest, components::build_platform(), channel, options.installed);
    }

    void adopt_offer(std::optional<UpdateOffer> found) {
        offer = std::move(found);
        phase = offer ? UpdatePhase::Available : UpdatePhase::Idle;
        if (offer) announce(*offer);
    }

    void announce(const UpdateOffer& found) {
        const SemVer& version = found.entry.version;
        if (mode == UpdateMode::NotifyOnly) {
            if (deps.state.get().announced_update == version) return;
            persist_state([version](storage::StateDocument& document) { document.announced_update = version; });
        } else {
            if (announced == version) return;
            announced = version;
        }
        publish(deps.events, UpdateAvailable{found});
    }

    void maybe_auto_apply() {
        if (mode != UpdateMode::InPlace || !offer || apply_active()) return;
        if (not_applied == offer->entry.version) return;
        (void)start_apply_job(ApplyWhen::WhenIdle);
    }

    Result<OpHandle> run_check(CheckTrigger trigger, DisconnectPolicy policy) {
        if (phase == UpdatePhase::Applying) return std::unexpected(busy());
        if (check) {
            if (check->op->done()) return std::unexpected(busy());
            // A user who joins a background check is told when it fails.
            if (trigger == CheckTrigger::User) check->trigger = CheckTrigger::User;
            return check->handle;
        }
        auto [handle, op] = deps.ops.create<std::optional<UpdateOffer>>(OpKind::UpdateCheck, policy, std::nullopt);
        check = CheckJob{&op, handle, trigger};
        periodic.cancel();
        if (!apply_active()) phase = UpdatePhase::Checking;
        op.progress(Progress{.phase = "check"});
        deps.manifest.refresh(op.token(), guarded([this](Result<components::ManifestRefresh> refreshed) {
            on_checked(std::move(refreshed));
        }));
        return handle;
    }

    void on_checked(Result<components::ManifestRefresh> refreshed) {
        const CheckJob job = *std::exchange(check, std::nullopt);
        const bool active = apply_active();
        if (!refreshed) {
            if (const std::optional<CancelReason> reason = job.op->token().reason()) {
                if (!active) phase = offer ? UpdatePhase::Available : UpdatePhase::Idle;
                job.op->complete(Cancelled{*reason});
                arm_periodic(options.check_interval);
                return;
            }
            Diagnostic failure = updates_diag(msg::kCheckFailed).retryable().cause(std::move(refreshed.error())).build();
            REBOOT_LOG_WARN(Update, "Cannot check for launcher updates: {}", failure.causes.front().id);
            last_error = failure;
            if (!active) phase = offer ? UpdatePhase::Available : UpdatePhase::Failed;
            // Background checks fail quietly; the state still carries the error.
            if (job.trigger == CheckTrigger::User) publish(deps.events, UpdateFailed{UpdateStage::Check, failure});
            job.op->complete(Failed{std::move(failure)});
            arm_periodic(options.check_interval);
            return;
        }

        const auto now = deps.clock.system_now();
        persist_state([now](storage::StateDocument& document) { document.last_update_check = now; });
        std::optional<UpdateOffer> found = select_current();
        if (!active) adopt_offer(found);
        job.op->complete(Completed<std::optional<UpdateOffer>>{std::move(found)});
        arm_periodic(options.check_interval);
        if (!active) maybe_auto_apply();
    }

    void arm_periodic(std::chrono::steady_clock::duration delay) {
        if (!auto_check) {
            periodic.cancel();
            return;
        }
        periodic = deps.timers.after(delay, [this] { on_periodic(); });
    }

    void on_periodic() {
        if (!auto_check) return;
        if (apply_active() || check) return arm_periodic(options.check_interval);
        (void)run_check(CheckTrigger::Periodic, DisconnectPolicy::BoundToConnection);
    }

    void set_auto_check(bool enabled) {
        auto_check = enabled;
        if (!enabled) {
            periodic.cancel();
            return;
        }
        if (periodic.active() || check) return;
        const std::chrono::steady_clock::duration interval = options.check_interval;
        const auto last = deps.state.get().last_update_check;
        const auto elapsed =
            last ? std::max(std::chrono::steady_clock::duration::zero(),
                            std::chrono::duration_cast<std::chrono::steady_clock::duration>(deps.clock.system_now() - *last))
                 : interval;
        if (elapsed >= interval) {
            on_periodic();
            return;
        }
        arm_periodic(interval - elapsed);
    }

    void set_channel(storage::UpdateChannel next) {
        if (next == channel) return;
        channel = next;
        // A check in flight selects with the new channel when it completes.
        if (apply_active() || check || deps.manifest.current() == nullptr) return;
        adopt_offer(select_current());
        maybe_auto_apply();
    }

    Result<OpHandle> start_apply(ApplyWhen when) {
        if (!offer) return updates_diag(msg::kNoUpdate).kind(ErrorKind::NotFound).fail();
        if (mode == UpdateMode::NotifyOnly)
            return updates_diag(msg::kNotifyOnly).arg("version", offer->entry.version).kind(ErrorKind::Unsupported).fail();
        if (phase == UpdatePhase::Applying) return std::unexpected(busy());
        return start_apply_job(when);
    }

    Result<OpHandle> start_apply_job(ApplyWhen when) {
        if (apply_job) {
            if (apply_job->op->done()) return std::unexpected(busy());
            if (when == ApplyWhen::Now) apply_job->when = ApplyWhen::Now;
            return apply_job->handle;
        }
        auto [handle, op] = deps.ops.create<ApplyStatus>(OpKind::UpdateApply, DisconnectPolicy::Detached, std::nullopt);
        apply_job.emplace();
        apply_job->op = &op;
        apply_job->handle = handle;
        apply_job->when = when;
        apply_job->entry = offer->entry;
        apply_job->on_cancel = op.token().on_cancel([this, id = handle.id()](CancelReason) {
            deps.strand.post(guarded([this, id] { on_apply_cancelled(id); }));
        });
        if (staged == apply_job->entry) {
            if (!gate.armed()) phase = UpdatePhase::Staged;
            deps.strand.post(guarded([this, id = handle.id()] {
                if (apply_job && apply_job->handle.id() == id) arm_staged();
            }));
        } else {
            start_download();
        }
        return handle;
    }

    // Only a cancelled or timed-out op is done before its work completes it.
    [[nodiscard]] bool job_abandoned() const { return !apply_job || apply_job->op->done(); }

    void abandon_job() {
        if (!apply_job) return;
        ApplyJob job = std::move(*std::exchange(apply_job, std::nullopt));
        // A drain already under way stays consented.
        if (phase != UpdatePhase::Draining) consented = false;
        if (!gate.armed()) phase = offer ? UpdatePhase::Available : UpdatePhase::Idle;
        job.op->complete(Cancelled{job.op->token().reason().value_or(CancelReason::User)});
    }

    void fail_job(UpdateStage stage, Diagnostic error) {
        if (!apply_job) return;
        ApplyJob job = std::move(*std::exchange(apply_job, std::nullopt));
        REBOOT_LOG_WARN(Update, "Launcher update {} failed: {}", job.entry.version.to_string(), error.id);
        consented = false;
        last_error = error;
        phase = UpdatePhase::Failed;
        publish(deps.events, UpdateFailed{stage, error});
        job.op->complete(Failed{std::move(error)});
    }

    void finish_job(ApplyStatus status) {
        if (!apply_job) return;
        ApplyJob job = std::move(*std::exchange(apply_job, std::nullopt));
        job.op->complete(Completed<ApplyStatus>{status});
    }

    void on_apply_cancelled(OpId id) {
        if (!apply_job || apply_job->handle.id() != id || !prompt) return;
        // A cancelled Now leaves the update staged behind the gate.
        withdraw_prompt(CancelReason::User);
        abandon_job();
    }

    void start_download() {
        phase = UpdatePhase::Downloading;
        const RemoteFile& package = apply_job->entry.package;
        apply_job->op->progress(Progress{.phase = "download", .done = 0, .total = package.size});
        const NativePath dir = download_root / apply_job->entry.version.to_string();
        NativePath file = dir / package_file_name(package, apply_job->entry.version);
        deps.workers.submit<void>(
            [&fs = deps.fs, dir](CancelToken) { return fs.create_dirs_owner_only(dir); }, apply_job->op->token(),
            deps.strand, guarded([this, file = std::move(file)](Result<void> created) mutable {
                if (job_abandoned()) return abandon_job();
                if (!created) return fail_job(UpdateStage::Download, download_failed({std::move(created.error())}));
                download_from(std::move(file), 0, {});
            }));
    }

    [[nodiscard]] Diagnostic download_failed(std::vector<Diagnostic> causes) const {
        Diagnostic failure =
            updates_diag(msg::kDownloadFailed).arg("version", apply_job->entry.version).retryable().build();
        failure.causes = std::move(causes);
        return failure;
    }

    // Each URL in order; a mirror's failure moves on to the next.
    void download_from(NativePath file, std::size_t index, std::vector<Diagnostic> causes) {
        const RemoteFile& package = apply_job->entry.package;
        if (index >= package.urls.size()) return fail_job(UpdateStage::Download, download_failed(std::move(causes)));
        net::DownloadRequest request{.url = package.urls[index], .file = file, .expected_size = package.size};
        auto progress = guarded([this](const net::DownloadProgress& at) {
            if (!job_abandoned())
                apply_job->op->progress(
                    Progress{.phase = "download", .done = at.done, .total = at.total, .rate_per_s = at.bytes_per_s});
        });
        auto done = guarded([this, file, index, causes](Result<net::DownloadResult> result) mutable {
            if (job_abandoned()) return abandon_job();
            if (!result) {
                causes.push_back(std::move(result.error()));
                return download_from(std::move(file), index + 1, std::move(causes));
            }
            verify(std::move(result->file));
        });
        Result<void> started =
            deps.downloader.start(std::move(request), apply_job->op->token(), std::move(progress), std::move(done));
        if (!started) {
            causes.push_back(std::move(started.error()));
            download_from(std::move(file), index + 1, std::move(causes));
        }
    }

    void verify(NativePath file) {
        const RemoteFile& package = apply_job->entry.package;
        apply_job->op->progress(Progress{.phase = "verify", .done = 0, .total = package.size});
        deps.workers.submit<void>(
            [&fs = deps.fs, file, package, version = apply_job->entry.version](CancelToken token) {
                return verify_package(fs, file, package, version, token);
            },
            apply_job->op->token(), deps.strand, guarded([this, file](Result<void> verified) mutable {
                if (job_abandoned()) return abandon_job();
                if (!verified) {
                    const UpdateStage stage =
                        verified.error().is(msg::kChecksumMismatch) ? UpdateStage::Verify : UpdateStage::Download;
                    return fail_job(stage, std::move(verified.error()));
                }
                stage(std::move(file));
            }));
    }

    void stage(NativePath file) {
        apply_job->op->progress(Progress{.phase = "stage"});
        deps.workers.submit<void>(
            [&applier = deps.applier, file = std::move(file)](CancelToken) { return applier.stage(file); },
            apply_job->op->token(), deps.strand, guarded([this](Result<void> staged_package) {
                if (staged_package && apply_job) staged = apply_job->entry;
                if (job_abandoned()) return abandon_job();
                if (!staged_package)
                    return fail_job(UpdateStage::Stage, updates_diag(msg::kStageFailed)
                                                            .arg("version", apply_job->entry.version)
                                                            .cause(std::move(staged_package.error()))
                                                            .build());
                arm_staged();
            }));
    }

    // Our own ops are live while they run; they must not hold the gate they are arming.
    [[nodiscard]] ActivitySnapshot without_own(ActivitySnapshot snapshot) const {
        std::erase_if(snapshot.live, [this](const LiveActivity& activity) {
            if (!activity.op) return false;
            return (apply_job && *activity.op == apply_job->handle.id()) || (check && *activity.op == check->handle.id());
        });
        return snapshot;
    }

    void arm_staged() {
        if (job_abandoned()) return abandon_job();
        const ActivitySnapshot now = without_own(deps.activity.snapshot());
        if (!gate.armed()) {
            phase = UpdatePhase::Staged;
            publish(deps.events, UpdateStaged{apply_job->entry, now.live});
        }
        if (now.idle()) {
            finish_job(ApplyStatus::Applying);
            gate.arm(now, [this] { on_gate_open(); });
            return;
        }
        if (!gate.armed()) gate.arm(now, [this] { on_gate_open(); });
        else gate.update(now);
        if (consented) {
            if (phase != UpdatePhase::Draining) begin_drain(now);
            finish_job(ApplyStatus::Draining);
            return;
        }
        phase = UpdatePhase::WaitingForIdle;
        if (apply_job->when == ApplyWhen::Now) {
            if (!prompt) ask_stop_sessions(now);
            return;
        }
        finish_job(ApplyStatus::WaitingForIdle);
    }

    void begin_drain(const ActivitySnapshot& now) {
        gate.drain_started(now);
        phase = UpdatePhase::Draining;
        deps.drain_for_update();
    }

    void ask_stop_sessions(const ActivitySnapshot& now) {
        const u64 serial = ++prompt_serial;
        prompt.emplace();
        prompt->serial = serial;
        const RequestId request = deps.requests.ask(
            UserRequestKind::ConfirmStopSessions, ConfirmStopSessionsPrompt{apply_job->entry.version, now.live},
            apply_job->handle.id(), std::nullopt,
            [this, serial, alive_token = alive.token()](const std::any& answer) -> Result<void> {
                const auto* reply = std::any_cast<ConfirmStopSessionsAnswer>(&answer);
                if (reply == nullptr) return updates_diag(msg::kAnswerInvalid).kind(ErrorKind::InvalidInput).fail();
                if (alive_token.cancelled()) return {};
                // Acted on after the registry resolved the request.
                deps.strand.post(guarded([this, serial, accept = reply->accept] { on_stop_answer(serial, accept); }));
                return {};
            },
            prompt->withdraw.token());
        apply_job->op->awaiting_user(request);
    }

    void withdraw_prompt(CancelReason reason) {
        if (!prompt) return;
        Prompt taken = std::move(*std::exchange(prompt, std::nullopt));
        taken.withdraw.cancel(reason);
    }

    void on_stop_answer(u64 serial, bool accept) {
        if (!prompt || prompt->serial != serial) return;
        prompt.reset();
        if (!apply_job) return;
        if (!accept) {
            ApplyJob job = std::move(*std::exchange(apply_job, std::nullopt));
            job.op->complete(Failed{updates_diag(msg::kStopDeclined)
                                        .severity(Severity::Warning)
                                        .kind(ErrorKind::Cancelled)
                                        .build()});
            return;
        }
        consented = true;
        begin_drain(without_own(deps.activity.snapshot()));
        finish_job(ApplyStatus::Draining);
    }

    Result<void> drain_consented() {
        if (!offer) return updates_diag(msg::kNoUpdate).kind(ErrorKind::NotFound).fail();
        if (mode == UpdateMode::NotifyOnly)
            return updates_diag(msg::kNotifyOnly).arg("version", offer->entry.version).kind(ErrorKind::Unsupported).fail();
        if (phase == UpdatePhase::Applying) return std::unexpected(busy());
        if (phase == UpdatePhase::Draining) return {};
        consented = true;
        if (apply_job) {
            apply_job->when = ApplyWhen::Now;
            // The client already asked, so our own question is moot.
            if (prompt) {
                withdraw_prompt(CancelReason::Superseded);
                begin_drain(without_own(deps.activity.snapshot()));
                finish_job(ApplyStatus::Draining);
            }
            return {};
        }
        if (gate.armed()) {
            begin_drain(without_own(deps.activity.snapshot()));
            return {};
        }
        Result<OpHandle> started = start_apply_job(ApplyWhen::Now);
        if (!started) return std::unexpected(std::move(started.error()));
        return {};
    }

    void on_activity_changed() {
        if (gate.armed()) gate.update(without_own(deps.activity.snapshot()));
    }

    void on_gate_open() {
        withdraw_prompt(CancelReason::Superseded);
        finish_job(ApplyStatus::Applying);
        begin_applying();
    }

    // resume.json, then the marker, then EngineUpdating and the restart.
    void begin_applying() {
        phase = UpdatePhase::Applying;
        const AppEntry entry = *staged;
        const ResumeRecord record = make_resume_record(options.origin, deps.activity.snapshot(), gate.drained());
        gate.disarm();
        Result<u64> stored =
            deps.resume.update([&record](storage::ResumeDocument& document) { store_resume_record(record, document); });
        if (!stored) return fail_apply(entry, std::move(stored.error()));
        deps.resume.flush(CancelToken{}, guarded([this, entry](Result<void> flushed) {
            if (!flushed) return fail_apply(entry, std::move(flushed.error()));
            write_marker(entry);
        }));
    }

    void write_marker(const AppEntry& entry) {
        const PendingUpdateMarker marker{options.installed, entry.version, 0, deps.clock.system_now()};
        deps.workers.submit<void>(
            [&fs = deps.fs, path = marker_path, bytes = encode_marker(marker)](CancelToken) -> Result<void> {
                if (auto created = fs.create_dirs_owner_only(path.parent_path()); !created) return created;
                return fs.atomic_replace(path, bytes, false);
            },
            CancelToken{}, deps.strand, guarded([this, entry](Result<void> written) {
                if (!written) return fail_apply(entry, std::move(written.error()));
                REBOOT_LOG_INFO(Update, "Restarting into launcher {}", entry.version.to_string());
                publish(deps.events, EngineUpdating{entry.version});
                // Runs after the shutdown steps, so it borrows nothing from this service.
                deps.quiesce([&applier = deps.applier, origin = options.origin,
                              version = entry.version]() -> Result<void> {
                    Result<void> applied = applier.apply_and_restart(resume_args(origin));
                    if (applied) return applied;
                    REBOOT_LOG_ERROR(Update, "Cannot install launcher {}: {}", version.to_string(), applied.error().id);
                    return updates_diag(msg::kApplyFailed)
                        .arg("version", version)
                        .cause(std::move(applied.error()))
                        .fail();
                });
            }));
    }

    // Nothing was stopped for the restart yet, so the engine keeps running the old version.
    void fail_apply(const AppEntry& entry, Diagnostic cause) {
        Diagnostic failure = updates_diag(msg::kApplyFailed).arg("version", entry.version).cause(std::move(cause)).build();
        REBOOT_LOG_ERROR(Update, "Cannot prepare the restart into launcher {}: {}", entry.version.to_string(),
                         failure.causes.front().id);
        consented = false;
        last_error = failure;
        phase = UpdatePhase::Failed;
        clear_resume();
        publish(deps.events, UpdateFailed{UpdateStage::Apply, std::move(failure)});
    }

    Result<void> admit_new_session() const {
        if (offer && offer->required && offer->entry.min_supported)
            return updates_diag(msg::kBelowMinSupported)
                .arg("installed", options.installed)
                .arg("min_supported", *offer->entry.min_supported)
                .kind(ErrorKind::Conflict)
                .fail();
        if (phase == UpdatePhase::Applying) return std::unexpected(busy());
        return {};
    }

    UpdateServiceDeps deps;
    UpdateOptions options;
    NativePath marker_path;
    NativePath download_root;
    UpdateMode mode;
    storage::UpdateChannel channel;
    bool auto_check;

    UpdatePhase phase = UpdatePhase::Idle;
    std::optional<UpdateOffer> offer;
    std::optional<Diagnostic> last_error;
    // The last version announced in InPlace mode; NotifyOnly persists it.
    std::optional<SemVer> announced;
    // A version whose apply did not take; only an explicit start_apply retries it.
    std::optional<SemVer> not_applied;
    std::optional<UpdateFailed> pending_failure;
    std::optional<PendingUpdateMarker> unconfirmed;

    std::optional<CheckJob> check;
    std::optional<ApplyJob> apply_job;
    // What IUpdateApplier holds staged in this run.
    std::optional<AppEntry> staged;
    // A Drain{Update} the user agreed to; it starts once the update is staged.
    bool consented = false;
    std::optional<Prompt> prompt;
    u64 prompt_serial = 0;
    ApplyGate gate;
    TimerHandle periodic;
    // Cancelled first in the destructor; callbacks still queued elsewhere check it.
    CancelSource alive;
};

UpdateService::UpdateService(UpdateServiceDeps deps, UpdateOptions options, const AppLayout& layout)
    : impl_(std::make_unique<Impl>(std::move(deps), std::move(options), layout)) {}

UpdateService::~UpdateService() = default;

Result<StartupResume> UpdateService::begin_startup() { return impl_->begin_startup(); }

Result<void> UpdateService::confirm_startup(Result<void> self_test) {
    return impl_->confirm_startup(std::move(self_test));
}

void UpdateService::start() { impl_->start(); }

void UpdateService::set_auto_check(bool enabled) { impl_->set_auto_check(enabled); }

void UpdateService::set_channel(storage::UpdateChannel channel) { impl_->set_channel(channel); }

UpdateState UpdateService::state() const { return impl_->state(); }

Result<OpHandle> UpdateService::start_check(CheckTrigger trigger, DisconnectPolicy policy) {
    return impl_->run_check(trigger, policy);
}

Result<OpHandle> UpdateService::start_apply(ApplyWhen when) { return impl_->start_apply(when); }

Result<void> UpdateService::drain_consented() { return impl_->drain_consented(); }

Result<void> UpdateService::admit_new_session() const { return impl_->admit_new_session(); }

}  // namespace rb::updates
