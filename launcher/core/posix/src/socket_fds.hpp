#pragma once

#include <cstddef>
#include <span>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace rb::posix {

// Every fd these return is close-on-exec and non-blocking, and writing to a socket never raises
// SIGPIPE: MSG_NOSIGNAL on Linux, SO_NOSIGPIPE on macOS.

[[nodiscard]] Result<UniqueFd> make_unix_stream_socket();
// An invalid fd when no connection is pending.
[[nodiscard]] Result<UniqueFd> accept_unix_stream(int listen_fd);
// For a socket we did not create, such as one systemd passed.
[[nodiscard]] Result<void> make_cloexec_nonblocking(int fd);

// errno of bind(2) or connect(2), or 0; `path` already passed check_socket_path_fits.
[[nodiscard]] int bind_unix(int socket_fd, const NativePath& path) noexcept;
[[nodiscard]] int connect_unix(int socket_fd, const NativePath& path) noexcept;

struct IoResult {
    std::size_t bytes = 0;
    // The call would block; nothing was transferred.
    bool would_block = false;
    // errno of a failed call; 0 on success.
    int error = 0;
};

[[nodiscard]] IoResult send_some(int socket_fd, std::span<const u8> bytes) noexcept;
// bytes == 0 without an error or would_block is end of stream.
[[nodiscard]] IoResult receive_some(int socket_fd, std::span<u8> out) noexcept;

// A self-pipe that wakes a poll() loop from another thread.
class WakePipe {
public:
    [[nodiscard]] static Result<WakePipe> create();

    // Thread-safe and async-signal-safe.
    void wake() const noexcept;
    // Empties the pipe; call when its read end polls readable.
    void drain() const noexcept;
    [[nodiscard]] int read_fd() const noexcept { return read_.get(); }

private:
    WakePipe(UniqueFd read, UniqueFd write) noexcept;

    UniqueFd read_;
    UniqueFd write_;
};

}  // namespace rb::posix
