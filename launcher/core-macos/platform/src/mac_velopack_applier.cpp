#include "darwin.hpp"

#include "reboot/os_macos/platform/mac_velopack_applier.hpp"

#include <Velopack.h>
#include <crt_externs.h>
#include <signal.h>
#include <sys/event.h>
#include <sys/wait.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <ctime>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

#include "apple_shims.hpp"
#include "kevent.hpp"
#include "messages.hpp"
#include "proc_info.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "run_program.hpp"

namespace reboot::os_macos::platform {

namespace {

constexpr std::string_view kUpdaterName = "UpdateMac";
constexpr std::string_view kEngineName = "reboot-engine";
constexpr std::string_view kPackageSuffix = ".nupkg";
constexpr std::chrono::seconds kKickstartDeadline{10};

[[nodiscard]] std::string last_velopack_error() {
    std::array<char, 1024> text{};
    (void)::vpkc_get_last_error(text.data(), text.size());
    text.back() = '\0';
    return std::string(text.data());
}

[[nodiscard]] Diagnostic outside_bundle() {
    return make_diag(ErrorDomain::Platform, kUpdateOutsideBundle).kind(ErrorKind::Unsupported).build();
}

[[nodiscard]] Diagnostic stage_failed(std::string detail) {
    return make_diag(ErrorDomain::Platform, kVelopackStageFailed).detail(std::move(detail)).build();
}

[[nodiscard]] Diagnostic apply_failed(std::string detail) {
    return make_diag(ErrorDomain::Platform, kVelopackApplyFailed).detail(std::move(detail)).build();
}

// An UpdateManager over our own layout: the bundle, its UpdateMac, and `feed_dir` as the packages
// directory, so staging never depends on where Velopack would keep packages by default.
class PendingUpdate {
public:
    PendingUpdate(const NativePath& bundle, const NativePath& feed_dir)
        : root_(bundle.native()),
          updater_((bundle / "Contents" / "MacOS" / std::string(kUpdaterName)).native()),
          packages_(feed_dir.native()),
          manifest_((bundle / "Contents" / "sq.version").native()),
          binaries_((bundle / "Contents" / "MacOS").native()) {}
    ~PendingUpdate() {
        if (asset_ != nullptr) ::vpkc_free_asset(asset_);
        if (manager_ != nullptr) ::vpkc_free_update_manager(manager_);
    }
    PendingUpdate(const PendingUpdate&) = delete;
    PendingUpdate& operator=(const PendingUpdate&) = delete;

    // False with the Velopack error when no manager could be made.
    [[nodiscard]] bool open() {
        vpkc_locator_config_t locator{};
        locator.RootAppDir = root_.data();
        locator.UpdateExePath = updater_.data();
        locator.PackagesDir = packages_.data();
        locator.ManifestPath = manifest_.data();
        locator.CurrentBinaryDir = binaries_.data();
        locator.IsPortable = true;
        vpkc_update_options_t options{};
        // Rollback ships as a lower version under a higher manifest serial.
        options.AllowVersionDowngrade = true;
        options.ExplicitChannel = nullptr;
        options.MaximumDeltasBeforeFallback = -1;
        return ::vpkc_new_update_manager(packages_.c_str(), &options, &locator, &manager_);
    }
    [[nodiscard]] bool find() { return ::vpkc_update_pending_restart(manager_, &asset_) && asset_ != nullptr; }

    [[nodiscard]] vpkc_update_manager_t* manager() const noexcept { return manager_; }
    [[nodiscard]] vpkc_asset_t* asset() const noexcept { return asset_; }

private:
    std::string root_;
    std::string updater_;
    std::string packages_;
    std::string manifest_;
    std::string binaries_;
    vpkc_update_manager_t* manager_ = nullptr;
    vpkc_asset_t* asset_ = nullptr;
};

[[nodiscard]] bool is_package(const NativePath& path) {
    const std::string name = path.filename().native();
    return name.size() > kPackageSuffix.size() && name.ends_with(kPackageSuffix);
}

[[nodiscard]] int reap(pid_t pid) noexcept {
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return 0;
    }
    return status;
}

// True once `pid` exited within `deadline`; the caller reaps it either way.
[[nodiscard]] Result<bool> wait_for_exit(pid_t pid, std::chrono::seconds deadline) {
    posix::UniqueFd queue{::kqueue()};
    if (!queue.valid()) return std::unexpected(posix::call_failed("kqueue", errno));
    const struct kevent watch =
        make_kevent(static_cast<std::uintptr_t>(pid), EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
    if (::kevent(queue.get(), &watch, 1, nullptr, 0, nullptr) != 0) {
        if (errno == ESRCH) return true;
        return std::unexpected(posix::call_failed("kevent", errno));
    }
    // An exit before the watch was armed is never reported.
    if (has_exited(static_cast<u32>(pid))) return true;
    const auto give_up = std::chrono::steady_clock::now() + deadline;
    for (;;) {
        const auto left = give_up - std::chrono::steady_clock::now();
        if (left <= std::chrono::steady_clock::duration::zero()) return false;
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(left);
        const timespec wait{static_cast<time_t>(seconds.count()),
                            static_cast<long>(std::chrono::duration_cast<std::chrono::nanoseconds>(left - seconds).count())};
        struct kevent event {};
        const int count = ::kevent(queue.get(), nullptr, 0, &event, 1, &wait);
        if (count > 0) return true;
        if (count < 0 && errno != EINTR) return std::unexpected(posix::call_failed("kevent", errno));
    }
}

// UpdateMac is spawned by Velopack as this process's child; it is told apart by its name.
[[nodiscard]] std::optional<pid_t> new_updater(const std::vector<u32>& before, const std::vector<u32>& after) {
    for (const u32 pid : after) {
        if (std::ranges::find(before, pid) != before.end()) continue;
        if (process_command_name(pid) == kUpdaterName) return static_cast<pid_t>(pid);
    }
    return std::nullopt;
}

}  // namespace

MacVelopackApplier::MacVelopackApplier(std::optional<NativePath> app_bundle, NativePath feed_dir,
                                       std::optional<std::string> launchd_label)
    : app_bundle_(std::move(app_bundle)), feed_dir_(std::move(feed_dir)), launchd_label_(std::move(launchd_label)) {}

Result<void> MacVelopackApplier::stage(const NativePath& package) {
    if (!app_bundle_) return std::unexpected(outside_bundle());
    std::error_code error;
    if (!std::filesystem::is_regular_file(package, error))
        return std::unexpected(posix::call_failed("stat", error ? error.value() : ENOENT, package));
    std::filesystem::create_directories(feed_dir_, error);
    if (error) return std::unexpected(posix::call_failed("mkdir", error.value(), feed_dir_));
    staged_version_.reset();

    // Velopack applies the newest package it finds, so only the one being staged may be there.
    for (std::filesystem::directory_iterator entry(feed_dir_, error), end; !error && entry != end; entry.increment(error)) {
        std::error_code ignored;
        if (std::filesystem::equivalent(entry->path(), package, ignored)) continue;
        if (is_package(entry->path()) || entry->path().extension() == ".partial") std::filesystem::remove(entry->path(), ignored);
    }
    if (error) return std::unexpected(posix::call_failed("readdir", error.value(), feed_dir_));
    const NativePath staged = feed_dir_ / package.filename();
    std::error_code same_error;
    if (!std::filesystem::equivalent(package, staged, same_error)) {
        NativePath partial = staged;
        partial += ".partial";
        std::filesystem::copy_file(package, partial, std::filesystem::copy_options::overwrite_existing, error);
        if (error) return std::unexpected(posix::call_failed("copyfile", error.value(), package));
        std::filesystem::rename(partial, staged, error);
        if (error) {
            const int code = error.value();
            std::filesystem::remove(partial, error);
            return std::unexpected(posix::call_failed("rename", code, staged));
        }
    }

    PendingUpdate pending(*app_bundle_, feed_dir_);
    if (!pending.open()) {
        std::filesystem::remove(staged, error);
        return std::unexpected(stage_failed(last_velopack_error()));
    }
    if (!pending.find() || pending.asset()->FileName == nullptr ||
        NativePath(pending.asset()->FileName).filename() != staged.filename() || pending.asset()->Version == nullptr) {
        std::filesystem::remove(staged, error);
        return std::unexpected(stage_failed("the package is not a full release of this app"));
    }
    Result<SemVer> version = SemVer::parse(pending.asset()->Version);
    if (!version) {
        std::filesystem::remove(staged, error);
        return std::unexpected(stage_failed(std::string("the package version ") + pending.asset()->Version + " is not SemVer"));
    }
    staged_version_ = std::move(*version);
    return {};
}

Result<void> MacVelopackApplier::apply_and_restart(std::vector<std::string> args) {
    if (!app_bundle_) return std::unexpected(outside_bundle());
    if (!staged_version_) return std::unexpected(apply_failed("no update is staged"));
    PendingUpdate pending(*app_bundle_, feed_dir_);
    if (!pending.open()) return std::unexpected(apply_failed(last_velopack_error()));
    if (!pending.find()) return std::unexpected(apply_failed("the staged package is gone"));

    const u32 self = static_cast<u32>(::getpid());
    Result<std::vector<u32>> before = child_pids(self);
    if (!before) return std::unexpected(std::move(before.error()));
    // No restart: Velopack would reopen the bundle, which starts the GUI and not this engine.
    if (!::vpkc_unsafe_apply_updates(pending.manager(), pending.asset(), true, 0, false, nullptr, 0))
        return std::unexpected(apply_failed(last_velopack_error()));
    Result<std::vector<u32>> after = child_pids(self);
    if (!after) return std::unexpected(std::move(after.error()));
    const std::optional<pid_t> updater = new_updater(*before, *after);
    if (!updater) return std::unexpected(apply_failed("the updater did not start"));

    Result<bool> finished = wait_for_exit(*updater, kSwapDeadline);
    if (!finished || !*finished) {
        // Past the deadline it must never swap the bundle under this still-running engine.
        ::kill(*updater, SIGKILL);
        (void)reap(*updater);
        if (!finished) return std::unexpected(std::move(finished.error()));
        return make_diag(ErrorDomain::Platform, kUpdateSwapTimeout).arg("deadline", kSwapDeadline).retryable().fail();
    }
    (void)reap(*updater);

    std::optional<SemVer> installed;
    if (const std::optional<std::string> text = shims::bundle_version(*app_bundle_))
        if (Result<SemVer> parsed = SemVer::parse(*text)) installed = std::move(*parsed);
    if (installed != staged_version_)
        return make_diag(ErrorDomain::Platform, kUpdateNotApplied).arg("version", *staged_version_).fail();

    const NativePath engine = *app_bundle_ / "Contents" / "MacOS" / std::string(kEngineName);
    std::vector<std::string> argv_storage;
    argv_storage.reserve(args.size() + 1);
    argv_storage.push_back(engine.native());
    for (std::string& arg : args) argv_storage.push_back(std::move(arg));
    std::vector<char*> argv;
    argv.reserve(argv_storage.size() + 1);
    for (std::string& arg : argv_storage) argv.push_back(arg.data());
    argv.push_back(nullptr);
    // Same pid, so launchd keeps tracking the job; every O_CLOEXEC watchdog pipe closes here.
    ::execve(engine.c_str(), argv.data(), *::_NSGetEnviron());
    const int exec_error = errno;

    if (!launchd_label_) return std::unexpected(posix::call_failed("execve", exec_error, engine));
    const std::string target = "gui/" + std::to_string(::getuid()) + "/" + *launchd_label_;
    // kickstart -k kills this engine and starts the updated one; returning means it did not.
    Result<ProgramResult> kicked = run_program("/bin/launchctl", {"kickstart", "-k", target}, kKickstartDeadline);
    if (!kicked) return std::unexpected(std::move(kicked.error()));
    return make_diag(ErrorDomain::Platform, kHelperFailed)
        .arg("program", "launchctl")
        .arg("exit_code", static_cast<i64>(kicked->code.value_or(-1)))
        .cause(posix::call_failed("execve", exec_error, engine))
        .detail(kicked->output)
        .fail();
}

}  // namespace reboot::os_macos::platform
