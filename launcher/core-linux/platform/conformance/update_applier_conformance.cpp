#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "conformance_support.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/os_linux/platform/linux_update_applier.hpp"
#include "reboot/os_linux/platform/tarball_layout.hpp"
#include "reboot/os_linux/platform/xdg_paths.hpp"
#include "update_package_writer.hpp"

using namespace rb;
using namespace rb::os_linux::platform;
using rb::os_linux::platform::test::Item;
using rb::os_linux::platform::test::Scratch;
using rb::os_linux::platform::test::write_package;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kOldShim = "#!/bin/sh\necho old shim\n";

void write_file(const NativePath& path, std::string_view text,
                fs::perms perms = fs::perms::owner_read | fs::perms::owner_write) {
    fs::create_directories(path.parent_path());
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file);
        file << text;
    }
    fs::permissions(path, perms);
}

[[nodiscard]] std::string read_file(const NativePath& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::string link_target(const NativePath& link) {
    std::error_code error;
    const NativePath target = fs::read_symlink(link, error);
    return error ? std::string{} : target.native();
}

// versions/<v> for each of `versions`, current and previous pointing at two of them, the shim in
// bin/, and an exe dir in versions/<running>.
struct TarballTree {
    Scratch scratch{"reboot-linux-update"};
    TarballLayout layout{scratch.dir.path() / "install"};

    TarballTree(std::initializer_list<std::string_view> versions, std::string_view current, std::string_view previous) {
        for (const std::string_view version : versions) write_file(layout.version_dir(version) / "marker", version);
        fs::create_symlink(NativePath{"versions"} / current, layout.current_link());
        fs::create_symlink(NativePath{"versions"} / previous, layout.previous_link());
        write_file(layout.shim(), kOldShim, fs::perms::owner_all);
    }

    [[nodiscard]] XdgPaths paths(std::string_view running) const {
        return XdgPaths::for_layout(scratch.dir.path() / "home", layout.version_dir(running));
    }

    // A release of `version` whose bin/reboot-engine is `engine`.
    [[nodiscard]] NativePath package(std::string_view version, std::string_view engine) const {
        const std::string top{version};
        const NativePath file = scratch.dir.path() / (top + ".tar.zst");
        write_package(file, {
                                {.path = "./" + top + "/", .type = S_IFDIR, .perm = 0755},
                                {.path = "./" + top + "/bin/", .type = S_IFDIR, .perm = 0755},
                                {.path = "./" + top + "/bin/reboot-engine", .perm = 0755, .content = std::string(engine)},
                            });
        return file;
    }

    [[nodiscard]] std::vector<std::string> versions() const {
        std::vector<std::string> names;
        for (const fs::directory_entry& entry : fs::directory_iterator(layout.versions_dir()))
            names.push_back(entry.path().filename().native());
        std::ranges::sort(names);
        return names;
    }
};

// Runs `body` in a forked child, since a successful apply replaces the process; its exit status.
[[nodiscard]] int in_child(UniqueFunction<int()> body) {
    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) ::_exit(body());
    int status = 0;
    REQUIRE(::waitpid(pid, &status, 0) == pid);
    REQUIRE(WIFEXITED(status));
    return WEXITSTATUS(status);
}

// fd 3 as systemd passes it: the applier keeps a copy of the socket found there.
[[nodiscard]] bool give_listen_socket(int (&pair)[2]) {
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0) return false;
    if (::dup2(pair[0], 3) != 3) return false;
    return ::setenv("LISTEN_PID", std::to_string(::getpid()).c_str(), 1) == 0;
}

[[nodiscard]] std::optional<ino_t> inode_of(int fd) {
    struct stat info {};
    if (::fstat(fd, &info) != 0) return std::nullopt;
    return info.st_ino;
}

}  // namespace

TEST_CASE("stage keeps current, previous and the running version and prunes the rest", "[linux_conformance]") {
    TarballTree tree({"0.7.0", "0.8.0", "0.9.0", "1.0.0", ".staging-left"}, "1.0.0", "0.9.0");
    const XdgPaths paths = tree.paths("0.7.0");
    REQUIRE(paths.install_kind() == ports::InstallKind::Tarball);
    LinuxUpdateApplier updater(paths, false);
    REQUIRE(updater.supports_in_place());

    REQUIRE(updater.stage(tree.package("1.1.0", "#!/bin/sh\nexit 0\n")));
    CHECK(tree.versions() == std::vector<std::string>{"0.7.0", "0.9.0", "1.0.0", "1.1.0"});
    CHECK(read_file(tree.layout.version_dir("1.1.0") / "bin" / "reboot-engine") == "#!/bin/sh\nexit 0\n");
    // Nothing live moves before apply.
    CHECK(link_target(tree.layout.current_link()) == "versions/1.0.0");
    CHECK(link_target(tree.layout.previous_link()) == "versions/0.9.0");
    CHECK(read_file(tree.layout.shim()) == kOldShim);
}

TEST_CASE("staging a version a link already names leaves its directory alone", "[linux_conformance]") {
    TarballTree tree({"0.9.0", "1.0.0"}, "1.0.0", "0.9.0");
    const XdgPaths paths = tree.paths("1.0.0");
    LinuxUpdateApplier updater(paths, false);

    REQUIRE(updater.stage(tree.package("1.0.0", "#!/bin/sh\nexit 0\n")));
    CHECK(tree.versions() == std::vector<std::string>{"0.9.0", "1.0.0"});
    CHECK(read_file(tree.layout.version_dir("1.0.0") / "marker") == "1.0.0");
    CHECK_FALSE(fs::exists(tree.layout.version_dir("1.0.0") / "bin"));
}

TEST_CASE("a failed execve restores both links, the shim and fd 3", "[linux_conformance]") {
    TarballTree tree({"0.9.0", "1.0.0"}, "1.0.0", "0.9.0");
    const XdgPaths paths = tree.paths("1.0.0");
    // No #! line, so execve fails with ENOEXEC.
    const NativePath package = tree.package("1.1.0", "not an executable\n");

    const int code = in_child([&]() -> int {
        int pair[2] = {-1, -1};
        if (!give_listen_socket(pair)) return 10;
        LinuxUpdateApplier updater(paths, false);
        if (!updater.stage(package)) return 11;
        // The IPC listener closed its fd 3 on drain, and something else took the number.
        int pipe_ends[2] = {-1, -1};
        if (::pipe2(pipe_ends, O_CLOEXEC) != 0 || ::dup3(pipe_ends[0], 3, O_CLOEXEC) != 3) return 12;
        const std::optional<ino_t> taken = inode_of(3);
        const Result<void> applied = updater.apply_and_restart({"run", "--origin=update"});
        if (applied) return 13;
        if (inode_of(3) != taken) return 14;
        if ((::fcntl(3, F_GETFD) & FD_CLOEXEC) == 0) return 15;
        return 0;
    });
    CHECK(code == 0);
    CHECK(link_target(tree.layout.current_link()) == "versions/1.0.0");
    CHECK(link_target(tree.layout.previous_link()) == "versions/0.9.0");
    CHECK(read_file(tree.layout.shim()) == kOldShim);
    CHECK_FALSE(fs::exists(tree.layout.shim().parent_path() / ".reboot-engine.previous"));
}

TEST_CASE("apply moves the links, installs the version's shim and execs it with the socket at fd 3",
          "[linux_conformance]") {
    TarballTree tree({"0.9.0", "1.0.0"}, "1.0.0", "0.9.0");
    const XdgPaths paths = tree.paths("1.0.0");
    const NativePath report = tree.scratch.dir.path() / "report";
    const std::string engine = "#!/bin/sh\n{ echo \"args=$*\"; echo \"fd3=$(readlink /proc/self/fd/3)\"; } > '" +
                               report.native() + ".tmp'\nmv '" + report.native() + ".tmp' '" + report.native() + "'\n";
    const NativePath package = tree.package("1.1.0", engine);
    int pair[2] = {-1, -1};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    const std::optional<ino_t> socket = inode_of(pair[0]);
    REQUIRE(socket);

    const int code = in_child([&]() -> int {
        if (::dup2(pair[0], 3) != 3) return 10;
        if (::setenv("LISTEN_PID", std::to_string(::getpid()).c_str(), 1) != 0) return 10;
        LinuxUpdateApplier updater(paths, false);
        if (!updater.stage(package)) return 11;
        // The IPC listener closed its copy on drain.
        ::close(3);
        (void)updater.apply_and_restart({"run", "--origin=update"});
        return 12;
    });
    ::close(pair[0]);
    ::close(pair[1]);
    CHECK(code == 0);
    const std::string written = read_file(report);
    INFO(written);
    CHECK(written.find("args=run --origin=update\n") != std::string::npos);
    CHECK(written.find("fd3=socket:[" + std::to_string(*socket) + "]\n") != std::string::npos);
    CHECK(link_target(tree.layout.current_link()) == "versions/1.1.0");
    CHECK(link_target(tree.layout.previous_link()) == "versions/1.0.0");
    CHECK(read_file(tree.layout.shim()) == engine);
}
