#include "reboot/posix/unix_socket_connector_base.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <poll.h>
#include <string>
#include <sys/socket.h>

#include "messages.hpp"
#include "reboot/posix/posix_error.hpp"
#include "socket_fds.hpp"
#include "socket_stream.hpp"
#include "unix_endpoint_checks.hpp"

namespace rb::posix {

namespace {

[[nodiscard]] Diagnostic engine_not_listening(const NativePath& socket_path) {
    return make_diag(ErrorDomain::Posix, kEngineNotListening)
        .arg("path", socket_path)
        .kind(ErrorKind::EngineUnavailable)
        .retryable();
}

// errno of a connect that was still in progress, or ETIMEDOUT after `deadline`.
[[nodiscard]] int finish_connect(int socket_fd, std::chrono::milliseconds deadline) noexcept {
    const auto until = std::chrono::steady_clock::now() + std::clamp(deadline, std::chrono::milliseconds{0},
                                                                      std::chrono::milliseconds{INT_MAX});
    for (;;) {
        const auto left = std::chrono::ceil<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
        const auto timeout = static_cast<int>(std::clamp<std::chrono::milliseconds::rep>(left.count(), 0, INT_MAX));
        pollfd fd{.fd = socket_fd, .events = POLLOUT, .revents = 0};
        const int ready = ::poll(&fd, 1, timeout);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return errno;
        if (ready == 0) return ETIMEDOUT;
        break;
    }
    int error = 0;
    socklen_t length = sizeof error;
    if (::getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0) return errno;
    return error;
}

}  // namespace

Result<std::unique_ptr<ports::IByteStream>> UnixSocketConnectorBase::connect(std::string_view endpoint_name,
                                                                             std::chrono::milliseconds deadline) {
    const NativePath socket_path{std::string(endpoint_name)};
    if (auto directory = check_private_directory(socket_path.parent_path(), peer_check_.expected_uid()); !directory) {
        if (directory.error().kind == ErrorKind::NotFound) return std::unexpected(engine_not_listening(socket_path));
        return std::unexpected(std::move(directory.error()));
    }
    if (auto fits = check_socket_path_fits(socket_path, sun_path_capacity()); !fits)
        return std::unexpected(std::move(fits.error()));

    auto socket = make_unix_stream_socket();
    if (!socket) return std::unexpected(std::move(socket.error()));
    int error = connect_unix(socket->get(), socket_path);
    // A non-blocking connect interrupted by a signal carries on, as with EINPROGRESS.
    if (error == EINPROGRESS || error == EINTR) error = finish_connect(socket->get(), deadline);
    // Linux reports a full backlog as EAGAIN: the engine is busy, and the client polls again.
    if (error == ENOENT || error == ECONNREFUSED || error == EAGAIN || error == ETIMEDOUT)
        return std::unexpected(engine_not_listening(socket_path));
    if (error != 0) return std::unexpected(call_failed("connect", error, socket_path));

    auto identity = peer_check_.verify(socket->get());
    if (!identity) return std::unexpected(std::move(identity.error()));
    auto stream = SocketStream::start(std::move(*socket), std::move(*identity));
    if (!stream) return std::unexpected(std::move(stream.error()));
    return std::unique_ptr<ports::IByteStream>{std::move(*stream)};
}

}  // namespace rb::posix
