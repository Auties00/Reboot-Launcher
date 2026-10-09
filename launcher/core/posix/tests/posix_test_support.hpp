#pragma once

#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <chrono>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/posix/peer_credential_check.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/testing/scratch_dir.hpp"
#include "reboot/testing/wall_clock_waiter.hpp"
#include "unistd.hpp"

// Real-OS helpers: this package only builds on macOS and Linux, so its tests double as the
// conformance run of the shared POSIX adapters.
namespace reboot::posix::test {

inline constexpr std::chrono::milliseconds kBudget{10000};

[[nodiscard]] inline testing::ScratchDir make_scratch(std::string_view prefix) {
    OsRandom random;
    auto dir = testing::ScratchDir::create(random, prefix);
    REQUIRE(dir.has_value());
    return std::move(*dir);
}

[[nodiscard]] inline bool wait_for(UniqueFunction<bool()> condition) {
    testing::WallClockWaiter waiter;
    return waiter.wait_until(std::move(condition), kBudget);
}

[[nodiscard]] inline u32 own_uid() noexcept { return static_cast<u32>(::geteuid()); }

// What the macOS and Linux adapters read: getpeereid there, SO_PEERCRED here.
[[nodiscard]] inline Result<PeerCredentials> read_peer(int socket_fd) {
#if defined(__linux__)
    ucred credentials{};
    socklen_t length = sizeof credentials;
    if (::getsockopt(socket_fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0)
        return std::unexpected(call_failed("getsockopt", errno));
    return PeerCredentials{.uid = static_cast<u32>(credentials.uid), .pid = static_cast<u32>(credentials.pid)};
#else
    uid_t uid = 0;
    gid_t gid = 0;
    if (::getpeereid(socket_fd, &uid, &gid) != 0) return std::unexpected(call_failed("getpeereid", errno));
    return PeerCredentials{.uid = static_cast<u32>(uid)};
#endif
}

// Reports every peer as the next uid up, standing in for a process of another user.
[[nodiscard]] inline Result<PeerCredentials> read_peer_as_stranger(int socket_fd) {
    auto peer = read_peer(socket_fd);
    if (peer) ++peer->uid;
    return peer;
}

[[nodiscard]] inline std::span<const u8> as_bytes(std::string_view text) noexcept {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

[[nodiscard]] inline u32 mode_of(const NativePath& path) {
    struct stat info {};
    REQUIRE(::lstat(path.c_str(), &info) == 0);
    return static_cast<u32>(info.st_mode) & 07777U;
}

[[nodiscard]] inline bool path_exists(const NativePath& path) noexcept {
    struct stat info {};
    return ::lstat(path.c_str(), &info) == 0;
}

// What a stream's callbacks saw; they run on the stream's thread.
class StreamLog {
public:
    void append(std::span<const u8> bytes) {
        const std::lock_guard lock{mutex_};
        received_.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    void closed() {
        const std::lock_guard lock{mutex_};
        ++closes_;
    }
    [[nodiscard]] std::string received() const {
        const std::lock_guard lock{mutex_};
        return received_;
    }
    [[nodiscard]] std::size_t size() const {
        const std::lock_guard lock{mutex_};
        return received_.size();
    }
    [[nodiscard]] int closes() const {
        const std::lock_guard lock{mutex_};
        return closes_;
    }

private:
    mutable std::mutex mutex_;
    std::string received_;
    int closes_ = 0;
};

// Restores the previous umask on destruction.
class UmaskGuard {
public:
    explicit UmaskGuard(mode_t mask) noexcept : previous_(::umask(mask)) {}
    ~UmaskGuard() { ::umask(previous_); }
    UmaskGuard(const UmaskGuard&) = delete;
    UmaskGuard& operator=(const UmaskGuard&) = delete;

private:
    mode_t previous_;
};

}  // namespace reboot::posix::test
