#include "reboot/os_linux/platform/xdg_shell.hpp"

#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <string>
#include <utility>

#include "file_uri.hpp"
#include "glib_runtime.hpp"
#include "helper_process.hpp"
#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace rb::os_linux::platform {

namespace {

constexpr const char* kLibrary = "libgio-2.0.so.0";
constexpr int kBusSession = 2;
constexpr int kCallTimeoutMs = 5000;
constexpr int kIoErrorNotFound = 1;
constexpr int kIoErrorNotSupported = 15;
constexpr std::chrono::milliseconds kDeadline{5000};

constexpr const char* kPortalName = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char* kOpenUri = "org.freedesktop.portal.OpenURI";

// GIO calls the shell makes; GVariantType arguments are type strings, as G_VARIANT_TYPE casts them.
struct GioApi {
    GLibRuntime glib;
    void* (*bus_get_sync)(int, void*, GErrorView**) = nullptr;
    void* (*call_sync)(void*, const char*, const char*, const char*, const char*, void*, const void*, int, int, void*,
                       GErrorView**) = nullptr;
    void* (*call_with_fds_sync)(void*, const char*, const char*, const char*, const char*, void*, const void*, int, int,
                                void*, void**, void*, GErrorView**) = nullptr;
    void* (*fd_list_new)() = nullptr;
    int (*fd_list_append)(void*, int, GErrorView**) = nullptr;
    void* (*variant_new)(const char*, ...) = nullptr;
    void (*variant_unref)(void*) = nullptr;
    void* (*builder_new)(const void*) = nullptr;
    void (*builder_add)(void*, const char*, ...) = nullptr;
    void (*builder_unref)(void*) = nullptr;
    void* (*file_new_for_path)(const char*) = nullptr;
    int (*file_trash)(void*, void*, GErrorView**) = nullptr;
    u32 (*io_error_quark)() = nullptr;

    [[nodiscard]] static std::optional<GioApi> load() {
        std::optional<GLibRuntime> glib = GLibRuntime::load(kLibrary);
        if (!glib) return std::nullopt;
        GioApi api{.glib = *glib};
        const bool complete = resolve(api.glib, "g_bus_get_sync", api.bus_get_sync) &&
                              resolve(api.glib, "g_dbus_connection_call_sync", api.call_sync) &&
                              resolve(api.glib, "g_dbus_connection_call_with_unix_fd_list_sync", api.call_with_fds_sync) &&
                              resolve(api.glib, "g_unix_fd_list_new", api.fd_list_new) &&
                              resolve(api.glib, "g_unix_fd_list_append", api.fd_list_append) &&
                              resolve(api.glib, "g_variant_new", api.variant_new) &&
                              resolve(api.glib, "g_variant_unref", api.variant_unref) &&
                              resolve(api.glib, "g_variant_builder_new", api.builder_new) &&
                              resolve(api.glib, "g_variant_builder_add", api.builder_add) &&
                              resolve(api.glib, "g_variant_builder_unref", api.builder_unref) &&
                              resolve(api.glib, "g_file_new_for_path", api.file_new_for_path) &&
                              resolve(api.glib, "g_file_trash", api.file_trash) &&
                              resolve(api.glib, "g_io_error_quark", api.io_error_quark);
        if (!complete) return std::nullopt;
        return api;
    }
};

// A GVariantBuilder, unreferenced once the call that consumed its contents is over.
class Builder {
public:
    Builder(const GioApi& api, const char* type) : api_(api), builder_(api.builder_new(type)) {}
    ~Builder() { api_.builder_unref(builder_); }
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;

    [[nodiscard]] void* get() const noexcept { return builder_; }

private:
    const GioApi& api_;
    void* builder_;
};

[[nodiscard]] Diagnostic no_opener(std::string_view target) {
    return make_diag(ErrorDomain::Platform, kNoOpener).arg("target", target);
}

}  // namespace

struct XdgShell::Impl {
    std::optional<GioApi> gio = GioApi::load();

    // Calls `method` on the session bus within kDeadline; true when it answered without an error.
    template <class Call>
    [[nodiscard]] bool on_session_bus(std::string_view method, Call&& call) const {
        if (!gio) return false;
        const Cancellable cancellable(gio->glib);
        const CancelAfter deadline(gio->glib, cancellable.get(), kDeadline);
        GErrorView* error = nullptr;
        void* const bus = gio->bus_get_sync(kBusSession, cancellable.get(), &error);
        if (bus == nullptr) {
            const std::string detail = gio->glib.take_error(error);
            REBOOT_LOG_DEBUG(Engine, "No session bus for {}: {}", method, detail);
            return false;
        }
        void* const reply = call(bus, cancellable.get(), &error);
        gio->glib.object_unref(bus);
        if (reply == nullptr) {
            const std::string detail = gio->glib.take_error(error);
            REBOOT_LOG_DEBUG(Engine, "{} failed: {}", method, detail);
            return false;
        }
        gio->variant_unref(reply);
        return true;
    }

    [[nodiscard]] bool portal_open_uri(const std::string& uri) const {
        return on_session_bus("OpenURI.OpenURI", [&](void* bus, void* cancellable, GErrorView** error) {
            const Builder options(*gio, "a{sv}");
            void* const parameters = gio->variant_new("(ssa{sv})", "", uri.c_str(), options.get());
            return gio->call_sync(bus, kPortalName, kPortalPath, kOpenUri, "OpenURI", parameters, nullptr, 0,
                                  kCallTimeoutMs, cancellable, error);
        });
    }

    // OpenFile or OpenDirectory: the portal takes an O_PATH fd, never a file:// URI.
    [[nodiscard]] bool portal_open_fd(const char* method, const NativePath& path) const {
        if (!gio) return false;
        const posix::UniqueFd fd{::open(path.c_str(), O_PATH | O_CLOEXEC)};
        if (!fd.valid()) return false;
        return on_session_bus(method, [&](void* bus, void* cancellable, GErrorView** error) -> void* {
            void* const fds = gio->fd_list_new();
            const int handle = gio->fd_list_append(fds, fd.get(), error);
            if (handle < 0) {
                gio->glib.object_unref(fds);
                return nullptr;
            }
            const Builder options(*gio, "a{sv}");
            void* const parameters = gio->variant_new("(sha{sv})", "", handle, options.get());
            void* const reply = gio->call_with_fds_sync(bus, kPortalName, kPortalPath, kOpenUri, method, parameters,
                                                        nullptr, 0, kCallTimeoutMs, fds, nullptr, cancellable, error);
            gio->glib.object_unref(fds);
            return reply;
        });
    }

    [[nodiscard]] bool file_manager_show(const NativePath& path) const {
        return on_session_bus("FileManager1.ShowItems", [&](void* bus, void* cancellable, GErrorView** error) {
            const std::string uri = file_uri(path);
            const Builder uris(*gio, "as");
            gio->builder_add(uris.get(), "s", uri.c_str());
            void* const parameters = gio->variant_new("(ass)", uris.get(), "");
            return gio->call_sync(bus, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                                  "org.freedesktop.FileManager1", "ShowItems", parameters, nullptr, 0, kCallTimeoutMs,
                                  cancellable, error);
        });
    }
};

namespace {

// xdg-open with the engine's environment; one still running at the deadline is showing the target.
[[nodiscard]] bool xdg_open(const std::string& target) {
    HelperCommand command;
    command.program = "xdg-open";
    command.args = {target};
    command.on_timeout = OnTimeout::Detach;
    const Result<HelperResult> ran = run_helper(command);
    if (!ran) {
        REBOOT_LOG_DEBUG(Engine, "xdg-open did not run: {}", ran.error().id);
        return false;
    }
    return ran->detached || ran->exit_code == 0;
}

}  // namespace

XdgShell::XdgShell() : impl_(std::make_unique<Impl>()) {}

XdgShell::~XdgShell() = default;

Result<void> XdgShell::open_url(std::string_view https_url) {
    if (!is_https_url(https_url))
        return make_diag(ErrorDomain::Platform, kUrlNotHttps).kind(ErrorKind::InvalidInput).fail();
    const std::string url{https_url};
    if (impl_->portal_open_uri(url) || xdg_open(url)) return {};
    return std::unexpected(no_opener(url));
}

Result<void> XdgShell::open_path(const NativePath& path) {
    if (impl_->portal_open_fd("OpenFile", path) || xdg_open(path.native())) return {};
    return std::unexpected(no_opener(display_utf8(path)));
}

Result<void> XdgShell::reveal(const NativePath& path) {
    if (impl_->file_manager_show(path) || impl_->portal_open_fd("OpenDirectory", path)) return {};
    return open_path(path.parent_path());
}

Result<void> XdgShell::trash(const NativePath& path) {
    const auto unavailable = [&] { return make_diag(ErrorDomain::Platform, kTrashUnavailable).arg("path", path); };
    if (!impl_->gio) return std::unexpected(unavailable().build());
    const GioApi& gio = *impl_->gio;
    void* const file = gio.file_new_for_path(path.c_str());
    const Cancellable cancellable(gio.glib);
    GErrorView* error = nullptr;
    int trashed = 0;
    {
        const CancelAfter deadline(gio.glib, cancellable.get(), kDeadline);
        trashed = gio.file_trash(file, cancellable.get(), &error);
    }
    gio.glib.object_unref(file);
    if (trashed != 0) return {};
    const bool io_error = error != nullptr && error->domain == gio.io_error_quark();
    const int code = error != nullptr ? error->code : 0;
    if (io_error && code == kIoErrorNotSupported) {
        gio.glib.error_free(error);
        return std::unexpected(unavailable().build());
    }
    Diagnostic failure = make_diag(ErrorDomain::Platform, kCallFailedOnPath)
                             .arg("call", "g_file_trash")
                             .arg("path", path)
                             .detail(gio.glib.take_error(error));
    if (io_error && code == kIoErrorNotFound) failure.kind = ErrorKind::NotFound;
    return std::unexpected(std::move(failure));
}

}  // namespace rb::os_linux::platform
