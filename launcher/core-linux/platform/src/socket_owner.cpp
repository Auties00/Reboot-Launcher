#include "socket_owner.hpp"

#include <array>
#include <cerrno>
#include <charconv>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>

#include "reboot/posix/unique_fd.hpp"
#include "text_files.hpp"

namespace rb::os_linux::platform {

namespace {

struct DirCloser {
    void operator()(DIR* listing) const noexcept { ::closedir(listing); }
};

using Listing = std::unique_ptr<DIR, DirCloser>;

[[nodiscard]] std::optional<u32> pid_of(std::string_view name) noexcept {
    u32 pid = 0;
    const auto [end, error] = std::from_chars(name.data(), name.data() + name.size(), pid);
    if (error != std::errc{} || end != name.data() + name.size() || pid == 0) return std::nullopt;
    return pid;
}

// Whether /proc/<pid>/fd holds a link to `target`.
[[nodiscard]] bool holds(u32 pid, std::string_view target) {
    const std::string fd_dir = "/proc/" + std::to_string(pid) + "/fd";
    posix::UniqueFd directory{::open(fd_dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (!directory.valid()) return false;
    Listing listing{::fdopendir(directory.get())};
    if (!listing) return false;
    const int directory_fd = directory.release();
    std::array<char, 64> link{};
    while (const dirent* entry = ::readdir(listing.get())) {
        if (entry->d_name[0] == '.') continue;
        const ssize_t length = ::readlinkat(directory_fd, entry->d_name, link.data(), link.size());
        if (length > 0 && std::string_view{link.data(), static_cast<std::size_t>(length)} == target) return true;
    }
    return false;
}

}  // namespace

bool is_wine_server(const NativePath& exe, std::string_view comm) {
    return exe.filename().native().starts_with("wineserver") || comm.starts_with("wineserver");
}

ports::PortOwner socket_owner(u64 inode) {
    const std::string target = "socket:[" + std::to_string(inode) + "]";
    Listing processes{::opendir("/proc")};
    if (!processes) return {};
    while (const dirent* entry = ::readdir(processes.get())) {
        const std::optional<u32> pid = pid_of(entry->d_name);
        if (!pid || !holds(*pid, target)) continue;
        ports::PortOwner owner;
        owner.pid = *pid;
        const NativePath proc = NativePath{"/proc"} / std::to_string(*pid);
        std::error_code error;
        NativePath exe = std::filesystem::read_symlink(proc / "exe", error);
        std::string comm = try_read_text_file(proc / "comm").value_or(std::string{});
        if (!comm.empty() && comm.back() == '\n') comm.pop_back();
        if (!error) owner.exe = exe;
        owner.wine_server = is_wine_server(error ? NativePath{} : exe, comm);
        return owner;
    }
    return {};
}

std::vector<SocketRecord> read_proc_net(std::string_view v4_name, std::string_view v6_name) {
    std::vector<SocketRecord> records;
    for (const auto& [name, v6] : {std::pair{v4_name, false}, std::pair{v6_name, true}}) {
        const std::optional<std::string> text = try_read_text_file(NativePath{"/proc/net"} / name);
        if (!text) continue;
        std::vector<SocketRecord> table = parse_proc_net(*text, v6);
        records.insert(records.end(), table.begin(), table.end());
    }
    return records;
}

}  // namespace rb::os_linux::platform
