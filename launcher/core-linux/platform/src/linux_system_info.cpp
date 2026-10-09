#include "reboot/os_linux/platform/linux_system_info.hpp"

#include <array>
#include <fcntl.h>
#include <string_view>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "os_release.hpp"
#include "proc_stat.hpp"
#include "process_environment.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "text_files.hpp"

namespace rb::os_linux::platform {

namespace {

// A pid reused during the walk cannot loop it forever.
constexpr int kMaxAncestors = 256;

constexpr std::array<std::string_view, 4> kCaBundles{
    "/etc/ssl/certs/ca-certificates.crt",
    "/etc/pki/tls/certs/ca-bundle.crt",
    "/etc/ssl/ca-bundle.pem",
    "/etc/ssl/cert.pem",
};

[[nodiscard]] bool readable_nonempty_file(const NativePath& path) {
    const posix::UniqueFd fd{::open(path.c_str(), O_RDONLY | O_CLOEXEC)};
    struct stat info {};
    return fd.valid() && ::fstat(fd.get(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

[[nodiscard]] std::optional<NativePath> find_ca_bundle() {
    if (const auto configured = env_value("SSL_CERT_FILE"); configured && configured->starts_with('/')) {
        if (readable_nonempty_file(NativePath{*configured})) return NativePath{*configured};
    }
    for (const std::string_view candidate : kCaBundles) {
        if (readable_nonempty_file(NativePath{candidate})) return NativePath{candidate};
    }
    return std::nullopt;
}

[[nodiscard]] bool has_steam_reaper_ancestor() {
    const std::optional<std::string> self = try_read_text_file("/proc/self/stat");
    const std::optional<ProcStat> own = self ? parse_proc_stat(*self) : std::nullopt;
    if (!own) return false;
    u32 pid = own->ppid;
    for (int depth = 0; pid >= 1 && depth < kMaxAncestors; ++depth) {
        const NativePath proc = NativePath{"/proc"} / std::to_string(pid);
        const std::optional<std::string> stat_text = try_read_text_file(proc / "stat");
        const std::optional<std::string> cmdline = try_read_text_file(proc / "cmdline");
        const std::optional<ProcStat> stat = stat_text ? parse_proc_stat(*stat_text) : std::nullopt;
        if (!stat || !cmdline) return false;
        if (is_steam_reaper(stat->comm, *cmdline)) return true;
        if (pid == 1) return false;
        pid = stat->ppid;
    }
    return false;
}

[[nodiscard]] bool exists(const char* path) {
    struct stat info {};
    return ::stat(path, &info) == 0;
}

[[nodiscard]] bool detect_container() {
    if (exists("/.dockerenv") || exists("/run/.containerenv") || exists("/.flatpak-info")) return true;
    if (const auto marker = try_read_text_file("/run/systemd/container"); marker && !marker->empty()) return true;
    const std::optional<std::string> init_environment = try_read_text_file("/proc/1/environ");
    return init_environment && environ_block_has(*init_environment, "container");
}

}  // namespace

LinuxSystemInfo::LinuxSystemInfo() {
    std::optional<std::string> release = try_read_text_file("/etc/os-release");
    if (!release) release = try_read_text_file("/usr/lib/os-release");
    const OsRelease parsed = release ? parse_os_release(*release) : OsRelease{};
    utsname machine{};
    const bool named = ::uname(&machine) == 0;
    os_.name = parsed.name.empty() ? std::string("Linux") : parsed.name;
    // Rolling distributions (Arch) have no VERSION_ID; the kernel release stands in.
    os_.version = !parsed.version_id.empty() ? parsed.version_id : named ? std::string(machine.release) : std::string();
    os_.build = parsed.build_id;
    os_.arch = named ? std::string(machine.machine) : std::string();
    elevated_ = ::geteuid() == 0;
    os_session_ = std::string(env_value("XDG_SESSION_ID").value_or(""));
    under_steam_reaper_ = has_steam_reaper_ancestor();
    ca_bundle_ = find_ca_bundle();
    in_container_ = detect_container();
}

}  // namespace rb::os_linux::platform
