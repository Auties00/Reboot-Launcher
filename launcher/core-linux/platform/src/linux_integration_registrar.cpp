#include "reboot/os_linux/platform/linux_integration_registrar.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include "desktop_files.hpp"
#include "helper_process.hpp"
#include "messages.hpp"
#include "process_environment.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/os_linux/platform/linux_file_system.hpp"
#include "reboot/os_linux/platform/tarball_layout.hpp"
#include "reboot/os_linux/platform/xdg_paths.hpp"
#include "reboot/posix/posix_error.hpp"
#include "systemd_units.hpp"
#include "systemd_user.hpp"
#include "text_files.hpp"

namespace reboot::os_linux::platform {

namespace {

using ports::IntegrationKind;
using ports::IntegrationState;
using ports::IntegrationStatus;

constexpr std::string_view kSchemeMime = "x-scheme-handler/reboot";
constexpr std::string_view kUrlEntryId = "reboot-launcher-url.desktop";
constexpr std::string_view kMenuEntryId = "reboot-launcher.desktop";
constexpr std::string_view kAutostartId = "reboot-launcher-engine.desktop";
constexpr std::string_view kBypassDetail = "bypasses-engine-agent";
constexpr std::string_view kNoSystemdDetail = "no-systemd";
constexpr std::string_view kDisabledDetail = "disabled";
constexpr std::array<std::string_view, 2> kServiceManagerArgs{"run", "--origin=service-manager"};

struct Files {
    NativePath url_entry;
    NativePath menu_entry;
    NativePath autostart;
    NativePath socket_unit;
    NativePath service_unit;
    NativePath mimeapps;
};

[[nodiscard]] Files files_of(const XdgPaths& paths) {
    const NativePath applications = paths.data_home() / "applications";
    const NativePath units = paths.config_home() / "systemd" / "user";
    return {.url_entry = applications / kUrlEntryId,
            .menu_entry = applications / kMenuEntryId,
            .autostart = paths.config_home() / "autostart" / kAutostartId,
            .socket_unit = units / kEngineSocketUnit,
            .service_unit = units / kEngineServiceUnit,
            .mimeapps = paths.config_home() / "mimeapps.list"};
}

[[nodiscard]] std::string_view entry_name(IntegrationKind kind) noexcept {
    switch (kind) {
        case IntegrationKind::UrlScheme: return "reboot://";
        case IntegrationKind::Autostart: return kAutostartId;
        case IntegrationKind::EngineAgent: return kEngineServiceUnit;
        case IntegrationKind::DesktopEntry: return kMenuEntryId;
    }
    return {};
}

[[nodiscard]] Result<std::optional<std::string>> read_optional(const NativePath& path) {
    Result<std::string> text = read_text_file(path);
    if (text) return std::move(*text);
    if (text.error().kind == ErrorKind::NotFound) return std::nullopt;
    return std::unexpected(std::move(text.error()));
}

[[nodiscard]] Result<void> write_file(const NativePath& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return std::unexpected(posix::call_failed("mkdir", error.value(), path.parent_path()));
    LinuxFileSystem files;
    return files.atomic_replace(path, {reinterpret_cast<const u8*>(text.data()), text.size()}, false);
}

[[nodiscard]] Result<void> remove_file(const NativePath& path) {
    if (::unlink(path.c_str()) != 0 && errno != ENOENT) return std::unexpected(posix::call_failed("unlink", errno, path));
    return {};
}

[[nodiscard]] bool file_exists(const NativePath& path) {
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0;
}

[[nodiscard]] IntegrationStatus make_status(IntegrationKind kind, IntegrationState state, std::string detail = {}) {
    return {.kind = kind, .state = state, .detail = std::move(detail)};
}

[[nodiscard]] std::vector<std::string> service_manager_command(const EngineCommand& engine) {
    std::vector<std::string> command{engine.exe.native()};
    command.insert(command.end(), engine.leading_args.begin(), engine.leading_args.end());
    for (const std::string_view arg : kServiceManagerArgs) command.emplace_back(arg);
    return command;
}

[[nodiscard]] bool is_systemctl_start(const std::vector<std::string>& args) {
    return !args.empty() && NativePath{args.front()}.filename() == "systemctl" &&
           std::ranges::contains(args, std::string("start")) &&
           std::ranges::contains(args, std::string(kEngineServiceUnit));
}

[[nodiscard]] std::vector<std::string> systemctl_start_command() {
    return {"systemctl", "--user", "start", std::string(kEngineServiceUnit)};
}

}  // namespace

namespace {

enum class Match : u8 { Ours, Stale, Foreign };

// The programs this install registers, which outlive any one version.
struct StableEntries {
    std::vector<NativePath> programs;
    std::optional<NativePath> tarball_root;
};

[[nodiscard]] StableEntries stable_entries(const XdgPaths& paths) {
    StableEntries stable;
    if (Result<EngineCommand> engine = stable_engine_command(paths)) stable.programs.push_back(engine->exe);
    if (paths.appimage()) stable.programs.push_back(*paths.appimage());
    stable.tarball_root = paths.tarball_root();
    return stable;
}

// Stale when the program is gone or is a version of this install rather than its stable entry.
[[nodiscard]] Match classify_program(const NativePath& program, const StableEntries& stable) {
    if (!file_exists(program)) return Match::Stale;
    if (std::ranges::contains(stable.programs, program)) return Match::Ours;
    if (stable.tarball_root) {
        if (is_inside(program, *stable.tarball_root / "current")) return Match::Ours;
        if (is_inside(program, *stable.tarball_root)) return Match::Stale;
    }
    return Match::Foreign;
}

[[nodiscard]] IntegrationState state_of(Match match) noexcept {
    switch (match) {
        case Match::Ours: return IntegrationState::Ours;
        case Match::Stale: return IntegrationState::Stale;
        case Match::Foreign: return IntegrationState::Foreign;
    }
    return IntegrationState::Foreign;
}

struct ParsedEntry {
    DesktopEntryKeys keys;
    std::optional<std::vector<std::string>> exec;
};

[[nodiscard]] Result<std::optional<ParsedEntry>> read_entry(const NativePath& path) {
    Result<std::optional<std::string>> text = read_optional(path);
    if (!text) return std::unexpected(std::move(text.error()));
    if (!*text) return std::nullopt;
    ParsedEntry entry{.keys = parse_desktop_entry(**text)};
    if (entry.keys.exec) entry.exec = split_desktop_exec(*entry.keys.exec);
    return entry;
}

// A desktop file of ours: what its Exec runs, judged against the stable entries.
[[nodiscard]] Result<IntegrationStatus> entry_status(IntegrationKind kind, const NativePath& path,
                                                     const StableEntries& stable) {
    Result<std::optional<ParsedEntry>> entry = read_entry(path);
    if (!entry) return std::unexpected(std::move(entry.error()));
    if (!*entry) return make_status(kind, IntegrationState::Absent);
    const std::optional<std::vector<std::string>>& exec = (*entry)->exec;
    const std::optional<NativePath> program = exec ? program_of(*exec) : std::nullopt;
    if (!program) return make_status(kind, IntegrationState::Stale);
    return make_status(kind, state_of(classify_program(*program, stable)), join_command(*exec));
}

// The XDG data directories' applications/, user first.
[[nodiscard]] std::vector<NativePath> application_dirs(const XdgPaths& paths) {
    std::vector<NativePath> dirs{paths.data_home() / "applications"};
    std::string_view data_dirs = env_value("XDG_DATA_DIRS").value_or("/usr/local/share:/usr/share");
    while (!data_dirs.empty()) {
        const std::size_t colon = data_dirs.find(':');
        const std::string_view dir = data_dirs.substr(0, colon);
        if (dir.starts_with('/')) dirs.push_back(NativePath{dir} / "applications");
        if (colon == std::string_view::npos) break;
        data_dirs.remove_prefix(colon + 1);
    }
    return dirs;
}

}  // namespace

namespace {

class Registrar {
public:
    Registrar(const XdgPaths& paths, const std::string& hash16, const std::optional<NativePath>& data_root_override)
        : paths_(paths), hash16_(hash16), override_(data_root_override), files_(files_of(paths)),
          stable_(stable_entries(paths)) {}

    [[nodiscard]] bool systemd_present() const {
        return paths_.runtime_dir() && systemd_user_manager_answers(*paths_.runtime_dir());
    }

    [[nodiscard]] bool units_installed() const { return file_exists(files_.service_unit); }

    // xdg-mime's answer, else the user's mimeapps.list.
    [[nodiscard]] std::optional<std::string> default_handler() const {
        HelperCommand query;
        query.program = "xdg-mime";
        query.args = {"query", "default", std::string(kSchemeMime)};
        query.capture_stdout = true;
        if (const Result<HelperResult> ran = run_helper(query); ran && ran->exit_code == 0) {
            std::string id = ran->output;
            while (!id.empty() && (id.back() == '\n' || id.back() == ' ')) id.pop_back();
            if (id.empty()) return std::nullopt;
            return id;
        }
        const std::optional<std::string> list = try_read_text_file(files_.mimeapps);
        return list ? mimeapps_default(*list, kSchemeMime) : std::nullopt;
    }

    [[nodiscard]] Result<IntegrationStatus> url_status() const {
        Result<IntegrationStatus> own = entry_status(IntegrationKind::UrlScheme, files_.url_entry, stable_);
        if (!own) return own;
        const std::optional<std::string> handler = default_handler();
        if (!handler) {
            if (own->state == IntegrationState::Absent) return own;
            return make_status(IntegrationKind::UrlScheme, IntegrationState::Stale, own->detail);
        }
        if (*handler == kUrlEntryId) {
            if (own->state == IntegrationState::Absent) return make_status(IntegrationKind::UrlScheme, IntegrationState::Stale);
            return own;
        }
        for (const NativePath& dir : application_dirs(paths_)) {
            Result<std::optional<ParsedEntry>> foreign = read_entry(dir / *handler);
            if (foreign && *foreign && (*foreign)->exec)
                return make_status(IntegrationKind::UrlScheme, IntegrationState::Foreign, join_command(*(*foreign)->exec));
        }
        return make_status(IntegrationKind::UrlScheme, IntegrationState::Foreign, *handler);
    }

    [[nodiscard]] Result<IntegrationStatus> autostart_status() const {
        Result<std::optional<ParsedEntry>> entry = read_entry(files_.autostart);
        if (!entry) return std::unexpected(std::move(entry.error()));
        if (!*entry) return make_status(IntegrationKind::Autostart, IntegrationState::Absent);
        const ParsedEntry& parsed = **entry;
        if (!parsed.exec) return make_status(IntegrationKind::Autostart, IntegrationState::Stale);
        const std::string command = join_command(*parsed.exec);
        const bool units = units_installed();
        if (is_systemctl_start(*parsed.exec)) {
            if (!units) return make_status(IntegrationKind::Autostart, IntegrationState::Stale, command);
            if (opted_out(parsed.keys))
                return make_status(IntegrationKind::Autostart, IntegrationState::Ours, std::string(kDisabledDetail));
            // What the started service runs, which is the command a direct entry would hold.
            return make_status(IntegrationKind::Autostart, IntegrationState::Ours, service_command().value_or(command));
        }
        const std::optional<NativePath> program = program_of(*parsed.exec);
        if (!program) return make_status(IntegrationKind::Autostart, IntegrationState::Stale, command);
        const Match match = classify_program(*program, stable_);
        if (match != Match::Ours) return make_status(IntegrationKind::Autostart, state_of(match), command);
        // A directly run engine would unlink the socket systemd listens on.
        if (units) return make_status(IntegrationKind::Autostart, IntegrationState::Stale, std::string(kBypassDetail));
        if (opted_out(parsed.keys))
            return make_status(IntegrationKind::Autostart, IntegrationState::Ours, std::string(kDisabledDetail));
        return make_status(IntegrationKind::Autostart, IntegrationState::Ours, command);
    }

    [[nodiscard]] std::optional<std::string> service_command() const {
        const std::optional<std::string> service = try_read_text_file(files_.service_unit);
        const std::optional<std::string> exec = service ? unit_value(*service, "ExecStart") : std::nullopt;
        const std::optional<std::vector<std::string>> args = exec ? split_unit_command(*exec) : std::nullopt;
        if (!args) return std::nullopt;
        return join_command(*args);
    }

    [[nodiscard]] Result<IntegrationStatus> agent_status() const {
        if (!systemd_present())
            return make_status(IntegrationKind::EngineAgent, IntegrationState::Absent, std::string(kNoSystemdDetail));
        Result<std::optional<std::string>> service = read_optional(files_.service_unit);
        if (!service) return std::unexpected(std::move(service.error()));
        Result<std::optional<std::string>> socket = read_optional(files_.socket_unit);
        if (!socket) return std::unexpected(std::move(socket.error()));
        if (!*service && !*socket) return make_status(IntegrationKind::EngineAgent, IntegrationState::Absent);
        if (!*service || !*socket) return make_status(IntegrationKind::EngineAgent, IntegrationState::Stale);
        const std::optional<std::string> exec = unit_value(**service, "ExecStart");
        const std::optional<std::vector<std::string>> args = exec ? split_unit_command(*exec) : std::nullopt;
        const std::optional<NativePath> program = args ? program_of(*args) : std::nullopt;
        if (!program) return make_status(IntegrationKind::EngineAgent, IntegrationState::Stale);
        const std::string command = join_command(*args);
        const Match match = classify_program(*program, stable_);
        // A changed XDG_DATA_HOME or data root moves the socket the units must listen on.
        if (match == Match::Ours && unit_value(**socket, "ListenStream") != engine_listen_stream(hash16_))
            return make_status(IntegrationKind::EngineAgent, IntegrationState::Stale, command);
        return make_status(IntegrationKind::EngineAgent, state_of(match), command);
    }

    [[nodiscard]] Result<IntegrationStatus> status(IntegrationKind kind) const {
        switch (kind) {
            case IntegrationKind::UrlScheme: return url_status();
            case IntegrationKind::Autostart: return autostart_status();
            case IntegrationKind::EngineAgent: return agent_status();
            case IntegrationKind::DesktopEntry: return entry_status(kind, files_.menu_entry, stable_);
        }
        return make_status(kind, IntegrationState::Absent);
    }

    // The stable form of a program in exe_dir.
    [[nodiscard]] Result<NativePath> stable_program(const NativePath& exe, IntegrationKind kind) const {
        if (paths_.appimage()) return *paths_.appimage();
        if (paths_.tarball_root()) {
            const NativePath relative = exe.lexically_relative(paths_.exe_dir());
            const bool within = !relative.empty() && *relative.begin() != "..";
            return *paths_.tarball_root() / "current" / (within ? relative : exe.filename());
        }
        return std::unexpected(needs_user_install(kind));
    }

    [[nodiscard]] Diagnostic needs_user_install(IntegrationKind kind) const {
        return make_diag(ErrorDomain::Platform, kIntegrationNeedsUserInstall)
            .arg("entry", entry_name(kind))
            .kind(ErrorKind::Unsupported);
    }

    [[nodiscard]] Result<void> set_default_handler(const std::optional<std::string>& id) const {
        if (id) {
            HelperCommand command;
            command.program = "xdg-mime";
            command.args = {"default", *id, std::string(kSchemeMime)};
            const Result<HelperResult> ran = run_helper(command);
            if (ran && ran->exit_code == 0) return {};
            if (ran) return std::unexpected(helper_failed("xdg-mime", *ran));
            if (ran.error().kind != ErrorKind::NotFound) return std::unexpected(ran.error());
        }
        // Without xdg-mime, or to unset, the user's mimeapps.list is edited directly.
        Result<std::optional<std::string>> list = read_optional(files_.mimeapps);
        if (!list) return std::unexpected(std::move(list.error()));
        if (!id && (!*list || mimeapps_default(**list, kSchemeMime) != std::optional<std::string>(kUrlEntryId)))
            return {};
        return write_file(files_.mimeapps, mimeapps_with_default(list->value_or(""), kSchemeMime, id));
    }

    [[nodiscard]] Result<void> write_autostart() const {
        std::vector<std::string> command;
        if (units_installed()) {
            command = systemctl_start_command();
        } else {
            Result<EngineCommand> engine = stable_engine_command(paths_);
            if (!engine) return std::unexpected(std::move(engine.error()));
            if (override_) command = {"env", "REBOOT_LAUNCHER_HOME=" + override_->native()};
            const std::vector<std::string> run = service_manager_command(*engine);
            command.insert(command.end(), run.begin(), run.end());
        }
        Result<std::optional<ParsedEntry>> existing = read_entry(files_.autostart);
        DesktopEntryKeys keep;
        if (existing && *existing) {
            keep.hidden = (*existing)->keys.hidden;
            keep.autostart_enabled = (*existing)->keys.autostart_enabled;
        }
        return write_file(files_.autostart,
                          render_desktop_entry("Reboot Launcher engine", command, {"NoDisplay=true"}, keep));
    }

    [[nodiscard]] Result<void> apply_agent() const {
        if (override_)
            return make_diag(ErrorDomain::Platform, kEngineAgentDefaultRootOnly).kind(ErrorKind::Unsupported).fail();
        if (!systemd_present())
            return make_diag(ErrorDomain::Platform, kSystemdUnavailable).kind(ErrorKind::Unsupported).fail();
        Result<EngineCommand> engine = stable_engine_command(paths_);
        if (!engine) return std::unexpected(std::move(engine.error()));
        const NativePath& runtime_dir = *paths_.runtime_dir();
        const std::string service = render_engine_service(service_manager_command(*engine), paths_.data_home(),
                                                          paths_.cache_home(), paths_.state_home());
        if (auto written = write_file(files_.socket_unit, render_engine_socket(hash16_)); !written) return written;
        if (auto written = write_file(files_.service_unit, service); !written) return written;
        if (auto reloaded = systemctl_user_ok({"daemon-reload"}, runtime_dir); !reloaded) return reloaded;
        if (auto enabled = systemctl_user_ok({"enable", std::string(kEngineSocketUnit)}, runtime_dir); !enabled)
            return enabled;
        // An engine already listening there keeps its socket until it exits.
        const NativePath socket_path = runtime_dir / "reboot-launcher" / (hash16_ + ".sock");
        if (!unix_socket_accepts(socket_path)) {
            if (auto started = systemctl_user_ok({"start", std::string(kEngineSocketUnit)}, runtime_dir); !started)
                return started;
        }
        if (file_exists(files_.autostart)) return write_autostart();
        return {};
    }

    [[nodiscard]] Result<void> apply(IntegrationKind kind, const NativePath& exe) const {
        switch (kind) {
            case IntegrationKind::UrlScheme: {
                Result<NativePath> program = stable_program(exe, kind);
                if (!program) return std::unexpected(std::move(program.error()));
                Result<IntegrationStatus> found = url_status();
                if (!found) return std::unexpected(std::move(found.error()));
                if (found->state == IntegrationState::Foreign)
                    return make_diag(ErrorDomain::Platform, kIntegrationForeign)
                        .arg("entry", entry_name(kind))
                        .kind(ErrorKind::Conflict)
                        .fail();
                const std::string entry =
                    render_desktop_entry("Reboot Launcher", {program->native(), "--activate-url", "%u"},
                                         {"NoDisplay=true", "MimeType=" + std::string(kSchemeMime) + ";"});
                if (auto written = write_file(files_.url_entry, entry); !written) return written;
                return set_default_handler(std::string(kUrlEntryId));
            }
            case IntegrationKind::DesktopEntry: {
                Result<NativePath> program = stable_program(exe, kind);
                if (!program) return std::unexpected(std::move(program.error()));
                return write_file(files_.menu_entry,
                                  render_desktop_entry("Reboot Launcher", {program->native()}, {"Categories=Game;"}));
            }
            case IntegrationKind::Autostart: return write_autostart();
            case IntegrationKind::EngineAgent: return apply_agent();
        }
        return {};
    }

    [[nodiscard]] Result<void> remove(IntegrationKind kind) const {
        Result<IntegrationStatus> found = status(kind);
        if (!found) return std::unexpected(std::move(found.error()));
        if (found->state != IntegrationState::Ours && found->state != IntegrationState::Stale) return {};
        switch (kind) {
            case IntegrationKind::UrlScheme:
                if (auto removed = remove_file(files_.url_entry); !removed) return removed;
                return set_default_handler(std::nullopt);
            case IntegrationKind::DesktopEntry: return remove_file(files_.menu_entry);
            case IntegrationKind::Autostart: return remove_file(files_.autostart);
            case IntegrationKind::EngineAgent: {
                if (systemd_present()) {
                    // The running engine is left alone; only its socket stops listening.
                    if (auto disabled = systemctl_user_ok({"disable", "--now", std::string(kEngineSocketUnit)},
                                                          *paths_.runtime_dir());
                        !disabled)
                        return disabled;
                }
                if (auto removed = remove_file(files_.socket_unit); !removed) return removed;
                if (auto removed = remove_file(files_.service_unit); !removed) return removed;
                if (systemd_present()) {
                    if (auto reloaded = systemctl_user_ok({"daemon-reload"}, *paths_.runtime_dir()); !reloaded)
                        return reloaded;
                }
                if (file_exists(files_.autostart)) return write_autostart();
                return {};
            }
        }
        return {};
    }

private:
    const XdgPaths& paths_;
    const std::string& hash16_;
    const std::optional<NativePath>& override_;
    Files files_;
    StableEntries stable_;
};

}  // namespace

LinuxIntegrationRegistrar::LinuxIntegrationRegistrar(const XdgPaths& paths, std::string root_hash16,
                                                     std::optional<NativePath> data_root_override)
    : paths_(paths), root_hash16_(std::move(root_hash16)), data_root_override_(std::move(data_root_override)) {}

Result<ports::IntegrationStatus> LinuxIntegrationRegistrar::status(ports::IntegrationKind kind) {
    return Registrar(paths_, root_hash16_, data_root_override_).status(kind);
}

Result<void> LinuxIntegrationRegistrar::apply(ports::IntegrationKind kind, const NativePath& exe) {
    return Registrar(paths_, root_hash16_, data_root_override_).apply(kind, exe);
}

Result<void> LinuxIntegrationRegistrar::remove(ports::IntegrationKind kind) {
    return Registrar(paths_, root_hash16_, data_root_override_).remove(kind);
}

}  // namespace reboot::os_linux::platform
