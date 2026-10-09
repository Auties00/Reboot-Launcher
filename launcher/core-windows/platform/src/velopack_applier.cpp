#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/velopack_applier.hpp"

#include <Velopack.h>

#include <array>
#include <filesystem>
#include <system_error>

#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "wide.hpp"
#include "win_error.hpp"

namespace reboot::os_windows::platform {

namespace {

using Hook = UniqueFunction<void(VelopackHook)>;

void run_hook(void* user, VelopackHook which) noexcept {
    try {
        (*static_cast<Hook*>(user))(which);
    } catch (...) {
        REBOOT_LOG_ERROR(Update, "internal.bug: a Velopack hook threw");
    }
}

void after_install(void* user, const char*) { run_hook(user, VelopackHook::AfterInstall); }
void before_update(void* user, const char*) { run_hook(user, VelopackHook::BeforeUpdate); }
void after_update(void* user, const char*) { run_hook(user, VelopackHook::AfterUpdate); }
void before_uninstall(void* user, const char*) { run_hook(user, VelopackHook::BeforeUninstall); }

[[nodiscard]] std::string last_velopack_error() {
    std::array<char, 1024> text{};
    (void)vpkc_get_last_error(text.data(), text.size());
    text.back() = '\0';
    return std::string(text.data());
}

[[nodiscard]] Diagnostic stage_failed(std::string detail) {
    return make_diag(ErrorDomain::Platform, kVelopackStageFailed).detail(std::move(detail));
}

[[nodiscard]] Diagnostic apply_failed(std::string detail) {
    return make_diag(ErrorDomain::Platform, kVelopackApplyFailed).detail(std::move(detail));
}

[[nodiscard]] Diagnostic not_supported() {
    return make_diag(ErrorDomain::Platform, kUpdateNotSupported).kind(ErrorKind::Unsupported);
}

// The manager and the pending full release it found in <root>\packages.
class PendingUpdate {
public:
    PendingUpdate() = default;
    ~PendingUpdate() {
        if (asset_ != nullptr) vpkc_free_asset(asset_);
        if (manager_ != nullptr) vpkc_free_update_manager(manager_);
    }
    PendingUpdate(const PendingUpdate&) = delete;
    PendingUpdate& operator=(const PendingUpdate&) = delete;

    // False with the Velopack error when no manager could be made over `feed_dir`.
    [[nodiscard]] bool open(const NativePath& feed_dir) {
        const std::string feed = narrow(feed_dir.native());
        return vpkc_new_update_manager(feed.c_str(), nullptr, nullptr, &manager_);
    }
    [[nodiscard]] bool find() { return vpkc_update_pending_restart(manager_, &asset_) && asset_ != nullptr; }

    [[nodiscard]] vpkc_update_manager_t* manager() const noexcept { return manager_; }
    [[nodiscard]] vpkc_asset_t* asset() const noexcept { return asset_; }

private:
    vpkc_update_manager_t* manager_ = nullptr;
    vpkc_asset_t* asset_ = nullptr;
};

}  // namespace

void run_velopack_startup(UniqueFunction<void(VelopackHook)> on_hook) {
    // Velopack calls back through a C pointer, and a hook run exits inside vpkc_app_run.
    static Hook hook;
    hook = std::move(on_hook);
    // Only the engine decides when to apply, after a drain.
    vpkc_app_set_auto_apply_on_startup(false);
    vpkc_app_set_hook_after_install(&after_install);
    vpkc_app_set_hook_before_update(&before_update);
    vpkc_app_set_hook_after_update(&after_update);
    vpkc_app_set_hook_before_uninstall(&before_uninstall);
    vpkc_app_run(&hook);
}

VelopackApplier::VelopackApplier(std::optional<NativePath> velopack_root, NativePath feed_dir)
    : velopack_root_(std::move(velopack_root)), feed_dir_(std::move(feed_dir)) {}

Result<void> VelopackApplier::stage(const NativePath& package) {
    if (!velopack_root_) return std::unexpected(not_supported());
    std::error_code error;
    if (!std::filesystem::is_regular_file(package, error))
        return std::unexpected(call_failed("stage", ERROR_FILE_NOT_FOUND, package));
    const NativePath packages = *velopack_root_ / "packages";
    std::filesystem::create_directories(packages, error);
    if (error) return std::unexpected(call_failed("CreateDirectoryW", static_cast<u32>(error.value()), packages));
    std::filesystem::create_directories(feed_dir_, error);
    if (error) return std::unexpected(call_failed("CreateDirectoryW", static_cast<u32>(error.value()), feed_dir_));

    // Velopack applies what its packages directory holds, so the verified package is copied there.
    const NativePath staged = packages / package.filename();
    NativePath partial = staged;
    partial += ".partial";
    const std::wstring source = extended_path(package);
    const std::wstring partial_path = extended_path(partial);
    const std::wstring staged_path = extended_path(staged);
    if (CopyFileW(source.c_str(), partial_path.c_str(), FALSE) == 0)
        return std::unexpected(call_failed("CopyFileW", GetLastError(), package));
    if (MoveFileExW(partial_path.c_str(), staged_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        const DWORD move_error = GetLastError();
        DeleteFileW(partial_path.c_str());
        return std::unexpected(call_failed("MoveFileExW", move_error, staged));
    }

    PendingUpdate pending;
    if (!pending.open(feed_dir_)) {
        DeleteFileW(staged_path.c_str());
        return std::unexpected(stage_failed(last_velopack_error()));
    }
    if (!pending.find() || pending.asset()->FileName == nullptr ||
        NativePath(widen(pending.asset()->FileName)).filename() != staged.filename()) {
        DeleteFileW(staged_path.c_str());
        return std::unexpected(stage_failed("the package is not a newer full release of this app"));
    }
    return {};
}

Result<void> VelopackApplier::apply_and_restart(std::vector<std::string> args) {
    if (!velopack_root_) return std::unexpected(not_supported());
    PendingUpdate pending;
    if (!pending.open(feed_dir_)) return std::unexpected(apply_failed(last_velopack_error()));
    if (!pending.find()) return std::unexpected(apply_failed("no staged update"));
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args) argv.push_back(arg.data());
    if (!vpkc_wait_exit_then_apply_updates(pending.manager(), pending.asset(), true, true, argv.data(), argv.size()))
        return std::unexpected(apply_failed(last_velopack_error()));
    // Update.exe waits for this process to exit before it swaps the install.
    ExitProcess(0);
}

}  // namespace reboot::os_windows::platform
