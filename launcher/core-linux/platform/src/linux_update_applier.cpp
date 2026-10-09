#include "reboot/os_linux/platform/linux_update_applier.hpp"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <iterator>
#include <memory>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "process_environment.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/os_linux/platform/linux_file_system.hpp"
#include "reboot/os_linux/platform/tarball_layout.hpp"
#include "reboot/os_linux/platform/xdg_paths.hpp"
#include "reboot/posix/posix_error.hpp"
#include "update_package.hpp"

namespace rb::os_linux::platform {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kBlockSize = 64 * 1024;
constexpr int kListenFd = 3;

[[nodiscard]] std::string random_suffix() {
    OsRandom random;
    return random_token_hex(random, 8);
}

[[nodiscard]] Diagnostic invalid(const NativePath& package) {
    return make_diag(ErrorDomain::Platform, kUpdatePackageInvalid).arg("path", package);
}

[[nodiscard]] Result<void> sync_directory(const NativePath& dir) {
    const posix::UniqueFd fd{::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (!fd.valid()) return std::unexpected(posix::call_failed("open", errno, dir));
    if (::fsync(fd.get()) != 0 && errno != EINVAL) return std::unexpected(posix::call_failed("fsync", errno, dir));
    return {};
}

// Copies `from` to a fresh 0755 file at `to` through a temporary beside it.
[[nodiscard]] Result<void> copy_executable(const NativePath& from, const NativePath& to) {
    const posix::UniqueFd source{::open(from.c_str(), O_RDONLY | O_CLOEXEC)};
    if (!source.valid()) return std::unexpected(posix::call_failed("open", errno, from));
    const NativePath temp = to.parent_path() / ("." + to.filename().native() + ".tmp-" + random_suffix());
    posix::UniqueFd target{::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0755)};
    if (!target.valid()) return std::unexpected(posix::call_failed("open", errno, temp));
    auto copied = [&]() -> Result<void> {
        std::vector<char> buffer(kBlockSize);
        for (;;) {
            const ssize_t got = ::read(source.get(), buffer.data(), buffer.size());
            if (got < 0 && errno == EINTR) continue;
            if (got < 0) return std::unexpected(posix::call_failed("read", errno, from));
            if (got == 0) break;
            for (ssize_t done = 0; done < got;) {
                const ssize_t written = ::write(target.get(), buffer.data() + done, static_cast<std::size_t>(got - done));
                if (written < 0 && errno == EINTR) continue;
                if (written < 0) return std::unexpected(posix::call_failed("write", errno, temp));
                done += written;
            }
        }
        // The umask may have narrowed the creation mode.
        if (::fchmod(target.get(), 0755) != 0) return std::unexpected(posix::call_failed("fchmod", errno, temp));
        if (::fsync(target.get()) != 0) return std::unexpected(posix::call_failed("fsync", errno, temp));
        if (::close(target.release()) != 0) return std::unexpected(posix::call_failed("close", errno, temp));
        if (::rename(temp.c_str(), to.c_str()) != 0) return std::unexpected(posix::call_failed("rename", errno, to));
        return sync_directory(to.parent_path());
    }();
    if (!copied) ::unlink(temp.c_str());
    return copied;
}

[[nodiscard]] std::optional<std::string> read_link(const NativePath& link) {
    std::error_code error;
    const NativePath target = fs::read_symlink(link, error);
    if (error) return std::nullopt;
    return target.native();
}

// Points `link` at `target` by renaming a fresh symlink over it.
[[nodiscard]] Result<void> replace_symlink(const NativePath& link, const std::string& target) {
    const NativePath temp = link.parent_path() / ("." + link.filename().native() + ".tmp-" + random_suffix());
    if (::symlink(target.c_str(), temp.c_str()) != 0) return std::unexpected(posix::call_failed("symlink", errno, temp));
    if (::rename(temp.c_str(), link.c_str()) != 0) {
        const int error = errno;
        ::unlink(temp.c_str());
        return std::unexpected(posix::call_failed("rename", error, link));
    }
    return sync_directory(link.parent_path());
}

// The version a tarball link names: the last component of its target.
[[nodiscard]] std::optional<std::string> linked_version(const NativePath& link) {
    const std::optional<std::string> target = read_link(link);
    if (!target) return std::nullopt;
    return NativePath{*target}.lexically_normal().filename().native();
}

[[nodiscard]] std::vector<char*> pointers(std::vector<std::string>& strings) {
    std::vector<char*> out;
    out.reserve(strings.size() + 1);
    for (std::string& text : strings) out.push_back(text.data());
    out.push_back(nullptr);
    return out;
}

// Replaces the process; returns only on failure. dup2 clears FD_CLOEXEC, so the new image
// inherits the socket at the fd systemd passed it on. Whatever the engine had at that fd since
// is put back when execve fails.
[[nodiscard]] Diagnostic exec_engine(const NativePath& program, std::vector<std::string> argv, int listen_socket) {
    std::vector<char*> arguments = pointers(argv);
    posix::UniqueFd saved;
    int saved_flags = -1;
    if (listen_socket >= 0) {
        saved_flags = ::fcntl(kListenFd, F_GETFD);
        if (saved_flags >= 0) {
            saved.reset(::fcntl(kListenFd, F_DUPFD_CLOEXEC, kListenFd + 1));
            if (!saved.valid()) return posix::call_failed("fcntl", errno);
        }
        if (::dup2(listen_socket, kListenFd) < 0) return posix::call_failed("dup2", errno);
    }
    ::execve(program.c_str(), arguments.data(), environ);
    const Diagnostic failure = posix::call_failed("execve", errno, program);
    if (listen_socket >= 0) {
        if (saved.valid()) {
            (void)::dup3(saved.get(), kListenFd, (saved_flags & FD_CLOEXEC) != 0 ? O_CLOEXEC : 0);
        } else {
            ::close(kListenFd);
        }
    }
    return failure;
}

// Undo steps, run newest first.
class Rollback {
public:
    void add(UniqueFunction<void()> step) { steps_.push_back(std::move(step)); }
    void run() {
        for (auto step = steps_.rbegin(); step != steps_.rend(); ++step) (*step)();
        steps_.clear();
    }

private:
    std::vector<UniqueFunction<void()>> steps_;
};

[[nodiscard]] Result<void> apply_tarball(const TarballLayout& layout, const std::string& version,
                                         std::vector<std::string> args, int listen_socket) {
    const std::optional<std::string> old_current = read_link(layout.current_link());
    const std::optional<std::string> old_previous = read_link(layout.previous_link());
    if (!old_current) return std::unexpected(posix::call_failed("readlink", ENOENT, layout.current_link()));
    const NativePath version_shim = layout.version_dir(version) / "bin" / "reboot-engine";
    const NativePath shim = layout.shim();
    const NativePath backup = shim.parent_path() / ".reboot-engine.previous";

    Rollback rollback;
    const auto fail = [&](Diagnostic failure) -> Result<void> {
        rollback.run();
        return std::unexpected(std::move(failure));
    };
    if (auto moved = replace_symlink(layout.previous_link(), *old_current); !moved) return fail(std::move(moved.error()));
    rollback.add([&] {
        if (old_previous)
            (void)replace_symlink(layout.previous_link(), *old_previous);
        else
            ::unlink(layout.previous_link().c_str());
    });
    if (auto moved = replace_symlink(layout.current_link(), "versions/" + version); !moved)
        return fail(std::move(moved.error()));
    rollback.add([&] { (void)replace_symlink(layout.current_link(), *old_current); });

    ::unlink(backup.c_str());
    if (::link(shim.c_str(), backup.c_str()) != 0 && errno != ENOENT)
        return fail(posix::call_failed("link", errno, backup));
    if (auto copied = copy_executable(version_shim, shim); !copied) return fail(std::move(copied.error()));
    rollback.add([&] {
        if (::rename(backup.c_str(), shim.c_str()) != 0) ::unlink(backup.c_str());
    });

    std::vector<std::string> argv{shim.native()};
    argv.insert(argv.end(), std::make_move_iterator(args.begin()), std::make_move_iterator(args.end()));
    return fail(exec_engine(shim, std::move(argv), listen_socket));
}

// Removes every entry of versions/ (old versions, leftover staging) that neither link targets
// and the engine does not run from; nothing while `current` cannot be read.
[[nodiscard]] Result<void> prune_versions(const TarballLayout& layout, const std::string& running,
                                          ports::IFileSystem& files) {
    const std::optional<std::string> current = linked_version(layout.current_link());
    if (!current) return {};
    const std::optional<std::string> previous = linked_version(layout.previous_link());
    std::error_code error;
    std::vector<NativePath> unlinked;
    for (fs::directory_iterator it{layout.versions_dir(), error}, end; !error && it != end; it.increment(error)) {
        const std::string name = it->path().filename().native();
        if (name != *current && name != previous && name != running) unlinked.push_back(it->path());
    }
    if (error) return std::unexpected(posix::call_failed("readdir", error.value(), layout.versions_dir()));
    for (const NativePath& entry : unlinked) {
        if (auto removed = files.remove_tree(entry); !removed) return removed;
    }
    return {};
}

// No shim and no rollback: the update replaces the AppImage and runs it.
[[nodiscard]] Result<void> apply_appimage(const NativePath& appimage, std::vector<std::string> args, int listen_socket) {
    const NativePath update = appimage.parent_path() / ("." + appimage.filename().native() + ".update");
    struct stat info {};
    if (::stat(update.c_str(), &info) != 0) return make_diag(ErrorDomain::Platform, kUpdateNotStaged).fail();
    if (::rename(update.c_str(), appimage.c_str()) != 0) return std::unexpected(posix::call_failed("rename", errno, appimage));
    std::vector<std::string> argv{appimage.native(), "engine"};
    argv.insert(argv.end(), std::make_move_iterator(args.begin()), std::make_move_iterator(args.end()));
    return std::unexpected(exec_engine(appimage, std::move(argv), listen_socket));
}

}  // namespace

LinuxUpdateApplier::LinuxUpdateApplier(const XdgPaths& paths, bool in_container)
    : paths_(paths), in_container_(in_container) {
    const std::optional<std::string_view> listen_pid = env_value("LISTEN_PID");
    if (!listen_pid || *listen_pid != std::to_string(::getpid())) return;
    struct stat info {};
    if (::fstat(kListenFd, &info) != 0 || !S_ISSOCK(info.st_mode)) return;
    // Kept apart from fd 3, which the IPC listener closes when it drains.
    posix::UniqueFd kept{::fcntl(kListenFd, F_DUPFD_CLOEXEC, kListenFd + 1)};
    if (kept.valid()) inherited_socket_ = std::move(kept);
}

bool LinuxUpdateApplier::supports_in_place() const {
    const ports::InstallKind kind = paths_.install_kind();
    return !in_container_ && (kind == ports::InstallKind::Tarball || kind == ports::InstallKind::AppImage);
}

Result<void> LinuxUpdateApplier::stage(const NativePath& package) {
    if (!supports_in_place())
        return make_diag(ErrorDomain::Platform, kUpdateNotifyOnly).kind(ErrorKind::Unsupported).fail();
    struct stat info {};
    if (::stat(package.c_str(), &info) != 0) return std::unexpected(posix::call_failed("stat", errno, package));
    if (!S_ISREG(info.st_mode)) return std::unexpected(invalid(package));

    if (paths_.install_kind() == ports::InstallKind::AppImage) {
        const NativePath& appimage = *paths_.appimage();
        return copy_executable(package, appimage.parent_path() / ("." + appimage.filename().native() + ".update"));
    }

    const TarballLayout layout{*paths_.tarball_root()};
    LinuxFileSystem files;
    if (auto pruned = prune_versions(layout, paths_.exe_dir().filename().native(), files); !pruned) return pruned;
    const NativePath staging = layout.versions_dir() / (".staging-" + random_suffix());
    if (::mkdir(staging.c_str(), 0755) != 0) return std::unexpected(posix::call_failed("mkdir", errno, staging));
    auto staged = [&]() -> Result<std::string> {
        Result<std::string> extracted = extract_update(package, staging);
        if (!extracted) return std::unexpected(std::move(extracted.error()));
        const std::string version = std::move(*extracted);
        struct stat top {};
        const NativePath target = layout.version_dir(version);
        if (::lstat(target.c_str(), &top) == 0) {
            // A linked version may be running; the copy already there is that same version.
            if (linked_version(layout.current_link()) == version || linked_version(layout.previous_link()) == version)
                return version;
            if (auto removed = files.remove_tree(target); !removed) return std::unexpected(std::move(removed.error()));
        }
        if (::rename((staging / version).c_str(), target.c_str()) != 0)
            return std::unexpected(posix::call_failed("rename", errno, target));
        if (auto synced = sync_directory(layout.versions_dir()); !synced) return std::unexpected(std::move(synced.error()));
        return version;
    }();
    (void)files.remove_tree(staging);
    if (!staged) return std::unexpected(std::move(staged.error()));
    staged_version_ = std::move(*staged);
    return {};
}

Result<void> LinuxUpdateApplier::apply_and_restart(std::vector<std::string> args) {
    if (!supports_in_place())
        return make_diag(ErrorDomain::Platform, kUpdateNotifyOnly).kind(ErrorKind::Unsupported).fail();
    const int listen_socket = inherited_socket_ ? inherited_socket_->get() : -1;
    if (paths_.install_kind() == ports::InstallKind::AppImage)
        return apply_appimage(*paths_.appimage(), std::move(args), listen_socket);
    if (!staged_version_) return make_diag(ErrorDomain::Platform, kUpdateNotStaged).fail();
    return apply_tarball(TarballLayout{*paths_.tarball_root()}, *staged_version_, std::move(args), listen_socket);
}

}  // namespace rb::os_linux::platform
