#include "mount_table.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <system_error>

#include "reboot/foundation/paths.hpp"

namespace reboot::os_linux::platform {

namespace {

constexpr std::array<std::string_view, 27> kPseudoTypes{
    "proc",      "sysfs",       "cgroup",     "cgroup2",  "tmpfs",   "devtmpfs",  "devpts",
    "securityfs", "pstore",     "efivarfs",   "bpf",      "tracefs", "debugfs",   "configfs",
    "fusectl",   "mqueue",      "hugetlbfs",  "autofs",   "binfmt_misc", "squashfs", "ramfs",
    "rpc_pipefs", "nsfs",       "selinuxfs",  "fuse.gvfsd-fuse", "fuse.portal", "fuse.lxcfs"};

constexpr std::array<std::string_view, 5> kNetworkTypes{"nfs", "nfs4", "cifs", "smb3", "fuse.sshfs"};

[[nodiscard]] std::vector<std::string_view> split_spaces(std::string_view line) {
    std::vector<std::string_view> fields;
    while (!line.empty()) {
        const std::size_t start = line.find_first_not_of(' ');
        if (start == std::string_view::npos) break;
        line.remove_prefix(start);
        const std::size_t end = line.find(' ');
        fields.push_back(line.substr(0, end));
        if (end == std::string_view::npos) break;
        line.remove_prefix(end);
    }
    return fields;
}

[[nodiscard]] bool has_option(std::string_view options, std::string_view wanted) {
    while (!options.empty()) {
        const std::size_t comma = options.find(',');
        if (options.substr(0, comma) == wanted) return true;
        if (comma == std::string_view::npos) break;
        options.remove_prefix(comma + 1);
    }
    return false;
}

[[nodiscard]] bool parse_device(std::string_view text, u32& major, u32& minor) {
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos) return false;
    const char* const begin = text.data();
    const char* const end = text.data() + text.size();
    const auto first = std::from_chars(begin, begin + colon, major);
    const auto second = std::from_chars(begin + colon + 1, end, minor);
    return first.ec == std::errc{} && first.ptr == begin + colon && second.ec == std::errc{} && second.ptr == end;
}

[[nodiscard]] int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// "/a" is an ancestor of "/a/b" and of itself, not of "/ab".
[[nodiscard]] bool root_contains(std::string_view ancestor, std::string_view root) noexcept {
    if (ancestor == "/" || ancestor == root) return true;
    return root.size() > ancestor.size() && root.starts_with(ancestor) && root[ancestor.size()] == '/';
}

}  // namespace

std::string unescape_octal(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto octal = [&](std::size_t at) { return text[at] >= '0' && text[at] <= '7'; };
        if (text[i] == '\\' && i + 3 < text.size() && octal(i + 1) && octal(i + 2) && octal(i + 3)) {
            out += static_cast<char>(((text[i + 1] - '0') << 6) | ((text[i + 2] - '0') << 3) | (text[i + 3] - '0'));
            i += 3;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::string unescape_hex(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 3 < text.size() && text[i + 1] == 'x' && hex_digit(text[i + 2]) >= 0 &&
            hex_digit(text[i + 3]) >= 0) {
            out += static_cast<char>((hex_digit(text[i + 2]) << 4) | hex_digit(text[i + 3]));
            i += 3;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::vector<MountEntry> parse_mountinfo(std::string_view text) {
    std::vector<MountEntry> entries;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);

        const std::vector<std::string_view> fields = split_spaces(line);
        const auto separator = std::find(fields.begin(), fields.end(), std::string_view{"-"});
        // Six fixed fields before the optional ones, and type, source and options after "-".
        if (separator == fields.end() || separator - fields.begin() < 6 || fields.end() - separator < 3) continue;
        MountEntry entry;
        if (!parse_device(fields[2], entry.major, entry.minor)) continue;
        entry.root = unescape_octal(fields[3]);
        entry.mount_point = NativePath{unescape_octal(fields[4])};
        entry.read_only = has_option(fields[5], "ro");
        entry.fs_type = unescape_octal(*(separator + 1));
        entry.source = unescape_octal(*(separator + 2));
        entries.push_back(std::move(entry));
    }
    return entries;
}

bool is_pseudo_fs(std::string_view fs_type) noexcept { return std::ranges::contains(kPseudoTypes, fs_type); }

bool is_network_fs(std::string_view fs_type) noexcept { return std::ranges::contains(kNetworkTypes, fs_type); }

std::vector<std::size_t> real_mounts(const std::vector<MountEntry>& entries, bool skip_binds) {
    std::vector<std::size_t> kept;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const MountEntry& entry = entries[i];
        if (is_pseudo_fs(entry.fs_type)) continue;
        const bool bind = std::ranges::any_of(kept, [&](std::size_t earlier) {
            const MountEntry& shown = entries[earlier];
            return shown.major == entry.major && shown.minor == entry.minor && root_contains(shown.root, entry.root);
        });
        if (skip_binds && bind) continue;
        kept.push_back(i);
    }
    return kept;
}

std::optional<std::size_t> containing_mount(const std::vector<MountEntry>& entries,
                                            const std::vector<std::size_t>& candidates, const NativePath& path) {
    std::optional<std::size_t> best;
    std::size_t best_length = 0;
    for (const std::size_t index : candidates) {
        const NativePath& mount = entries[index].mount_point;
        if (!is_inside(path, mount)) continue;
        const std::size_t length = mount.native().size();
        if (!best || length >= best_length) {
            best = index;
            best_length = length;
        }
    }
    return best;
}

}  // namespace reboot::os_linux::platform
