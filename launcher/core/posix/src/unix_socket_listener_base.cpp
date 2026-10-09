#include "reboot/posix/unix_socket_listener_base.hpp"

#include <array>
#include <cerrno>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <system_error>
#include <thread>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/posix/posix_error.hpp"
#include "socket_fds.hpp"
#include "socket_stream.hpp"
#include "unistd.hpp"
#include "unix_endpoint_checks.hpp"

namespace rb::posix {

namespace {

// After an accept error such as EMFILE the socket stays readable, so polling it again at once would spin.
constexpr int kAcceptBackoffMs = 100;

// A socket that still accepts a connection belongs to a live listener; anything else is stale.
[[nodiscard]] Result<void> remove_stale_socket(const NativePath& socket_path) {
    struct stat info {};
    if (::lstat(socket_path.c_str(), &info) != 0) {
        if (errno == ENOENT) return {};
        return std::unexpected(call_failed("lstat", errno, socket_path));
    }
    if (S_ISSOCK(info.st_mode)) {
        auto probe = make_unix_stream_socket();
        if (!probe) return std::unexpected(std::move(probe.error()));
        const int error = connect_unix(probe->get(), socket_path);
        // EAGAIN is a full backlog on Linux, EINPROGRESS a pending connect elsewhere.
        if (error == 0 || error == EAGAIN || error == EINPROGRESS)
            return make_diag(ErrorDomain::Posix, kEndpointInUse).arg("path", socket_path).kind(ErrorKind::Conflict).fail();
        if (error != ECONNREFUSED && error != ENOENT) return std::unexpected(call_failed("connect", error, socket_path));
    }
    if (::unlink(socket_path.c_str()) != 0 && errno != ENOENT)
        return std::unexpected(call_failed("unlink", errno, socket_path));
    return {};
}

[[nodiscard]] Result<UniqueFd> bind_listening_socket(const NativePath& socket_path) {
    auto socket = make_unix_stream_socket();
    if (!socket) return std::unexpected(std::move(socket.error()));
    if (auto removed = remove_stale_socket(socket_path); !removed) return std::unexpected(std::move(removed.error()));
    if (const int error = bind_unix(socket->get(), socket_path); error != 0)
        return std::unexpected(call_failed("bind", error, socket_path));
    // bind honours the umask; the 0700 directory keeps the socket private until this chmod.
    if (::chmod(socket_path.c_str(), 0600) != 0) {
        const int error = errno;
        ::unlink(socket_path.c_str());
        return std::unexpected(call_failed("chmod", error, socket_path));
    }
    if (::listen(socket->get(), SOMAXCONN) != 0) {
        const int error = errno;
        ::unlink(socket_path.c_str());
        return std::unexpected(call_failed("listen", error, socket_path));
    }
    return socket;
}

}  // namespace

struct UnixSocketListenerBase::Impl {
    explicit Impl(PeerCredentialCheck check) noexcept : peer_check(std::move(check)) {}

    void run();
    // False after an accept error, so the caller backs off before polling the socket again.
    [[nodiscard]] bool accept_pending();
    void stop() noexcept;

    PeerCredentialCheck peer_check;
    UniqueFd listen_fd;
    // Empty for an inherited socket, whose path belongs to the service manager.
    NativePath bound_path;
    std::optional<WakePipe> wake;
    UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> on_accept;

    std::mutex mutex;
    bool stopping = false;
    std::thread thread;
};

UnixSocketListenerBase::UnixSocketListenerBase(PeerCredentialCheck peer_check)
    : impl_(std::make_unique<Impl>(std::move(peer_check))) {}

UnixSocketListenerBase::~UnixSocketListenerBase() { impl_->stop(); }

Result<std::optional<UniqueFd>> UnixSocketListenerBase::take_inherited_socket(const NativePath&) {
    return std::optional<UniqueFd>{};
}

Result<void> UnixSocketListenerBase::listen(std::string_view endpoint_name,
                                            UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> on_accept) {
    if (impl_->thread.joinable()) return std::unexpected(internal_bug("posix.listen_twice"));
    const NativePath socket_path{std::string(endpoint_name)};
    if (auto directory = ensure_private_directory(socket_path.parent_path(), impl_->peer_check.expected_uid());
        !directory)
        return directory;
    if (auto fits = check_socket_path_fits(socket_path, sun_path_capacity()); !fits) return fits;

    auto inherited = take_inherited_socket(socket_path);
    if (!inherited) return std::unexpected(std::move(inherited.error()));
    UniqueFd socket;
    NativePath bound_path;
    if (*inherited) {
        socket = std::move(**inherited);
        if (auto ready = make_cloexec_nonblocking(socket.get()); !ready) return ready;
    } else {
        auto bound = bind_listening_socket(socket_path);
        if (!bound) return std::unexpected(std::move(bound.error()));
        socket = std::move(*bound);
        bound_path = socket_path;
    }

    auto wake = WakePipe::create();
    if (!wake) {
        if (!bound_path.empty()) ::unlink(bound_path.c_str());
        return std::unexpected(std::move(wake.error()));
    }
    impl_->listen_fd = std::move(socket);
    impl_->bound_path = std::move(bound_path);
    impl_->wake.emplace(std::move(*wake));
    impl_->on_accept = std::move(on_accept);
    try {
        impl_->thread = std::thread([impl = impl_.get()] { impl->run(); });
    } catch (const std::system_error& error) {
        impl_->stop();
        return std::unexpected(call_failed("pthread_create", error.code().value()));
    }
    return {};
}

void UnixSocketListenerBase::close() { impl_->stop(); }

void UnixSocketListenerBase::Impl::run() {
    try {
        bool backing_off = false;
        for (;;) {
            {
                const std::lock_guard lock{mutex};
                if (stopping) return;
            }
            std::array<pollfd, 2> fds{pollfd{.fd = wake->read_fd(), .events = POLLIN, .revents = 0},
                                      pollfd{.fd = backing_off ? -1 : listen_fd.get(), .events = POLLIN, .revents = 0}};
            const int ready = ::poll(fds.data(), fds.size(), backing_off ? kAcceptBackoffMs : -1);
            if (ready < 0) {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "poll");
            }
            if (fds[0].revents != 0) wake->drain();
            if (backing_off) {
                if (ready == 0) backing_off = false;
                continue;
            }
            if (fds[1].revents != 0) backing_off = !accept_pending();
        }
    } catch (...) {
        try {
            REBOOT_LOG_ERROR(Ipc, "{} in the engine socket accept thread", internal_bug("posix.socket_listener").id);
        } catch (...) {
        }
    }
}

bool UnixSocketListenerBase::Impl::accept_pending() {
    for (;;) {
        auto accepted = accept_unix_stream(listen_fd.get());
        if (!accepted) {
            REBOOT_LOG_WARN(Ipc, "accept on the engine socket failed: {}", accepted.error().id);
            return false;
        }
        if (!accepted->valid()) return true;
        auto identity = peer_check.verify(accepted->get());
        if (!identity) {
            REBOOT_LOG_WARN(Ipc, "closed an engine socket peer: {}", identity.error().id);
            continue;
        }
        auto stream = SocketStream::start(std::move(*accepted), std::move(*identity));
        if (!stream) {
            REBOOT_LOG_WARN(Ipc, "dropped an engine socket peer: {}", stream.error().id);
            continue;
        }
        on_accept(std::move(*stream));
    }
}

void UnixSocketListenerBase::Impl::stop() noexcept {
    if (thread.joinable()) {
        {
            const std::lock_guard lock{mutex};
            stopping = true;
        }
        wake->wake();
        thread.join();
        stopping = false;
    }
    listen_fd.reset();
    if (!bound_path.empty()) ::unlink(bound_path.c_str());
    bound_path.clear();
    wake.reset();
    on_accept = nullptr;
}

}  // namespace rb::posix
