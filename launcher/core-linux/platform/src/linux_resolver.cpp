#include "reboot/os_linux/platform/linux_resolver.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <utility>
#include <vector>

#include "addrinfo_addresses.hpp"
#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/posix/posix_error.hpp"

namespace reboot::os_linux::platform {

namespace {

using Done = UniqueFunction<void(Result<std::vector<IpAddress>>)>;

struct Lookup {
    std::string host;
    Done done;
    // Whoever flips it first runs `done`: the worker or the cancelling thread.
    std::atomic<bool> finished{false};
    CancelRegistration registration;

    void finish(Result<std::vector<IpAddress>> result) {
        if (finished.exchange(true)) return;
        try {
            done(std::move(result));
        } catch (...) {
            REBOOT_LOG_ERROR(Net, "internal.bug: a resolve callback threw");
        }
    }
};

[[nodiscard]] Diagnostic cancelled(const std::string& host) {
    return make_diag(ErrorDomain::Platform, kDnsCancelled).arg("host", host).kind(ErrorKind::Cancelled);
}

[[nodiscard]] Diagnostic lookup_failed(const std::string& host, int code, int system_errno) {
    Diagnostic diag = make_diag(ErrorDomain::Platform, kDnsFailed).arg("host", host).detail(::gai_strerror(code));
    if (code == EAI_SYSTEM) diag.os_error = posix::errno_error(system_errno);
    diag.retryable = code == EAI_AGAIN;
    if (code == EAI_NONAME || code == EAI_FAIL || code == EAI_NODATA) diag.kind = ErrorKind::NotFound;
    return diag;
}

[[nodiscard]] Result<std::vector<IpAddress>> lookup(const std::string& host) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;
    addrinfo* list = nullptr;
    const int code = ::getaddrinfo(host.c_str(), nullptr, &hints, &list);
    const int system_errno = errno;
    if (code != 0) return std::unexpected(lookup_failed(host, code, system_errno));
    std::vector<IpAddress> addresses = addrinfo_addresses(list);
    ::freeaddrinfo(list);
    if (addresses.empty()) return std::unexpected(lookup_failed(host, EAI_NONAME, 0));
    return addresses;
}

struct Queue {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::shared_ptr<Lookup>> pending;
    // Inside getaddrinfo; the resolver's destructor finishes them as cancelled.
    std::vector<std::shared_ptr<Lookup>> running;
    // Workers running a `done`, which the destructor waits out.
    int delivering = 0;
    bool stopping = false;
};

void serve(const std::shared_ptr<Queue>& queue) {
    for (;;) {
        std::shared_ptr<Lookup> next;
        {
            std::unique_lock lock(queue->mutex);
            queue->changed.wait(lock, [&] { return queue->stopping || !queue->pending.empty(); });
            if (queue->stopping) return;
            next = std::move(queue->pending.front());
            queue->pending.pop_front();
            queue->running.push_back(next);
        }
        Result<std::vector<IpAddress>> result = next->finished ? Result<std::vector<IpAddress>>{} : lookup(next->host);
        {
            const std::lock_guard lock(queue->mutex);
            std::erase(queue->running, next);
            // The destructor already finished it.
            if (queue->stopping) return;
            ++queue->delivering;
        }
        next->finish(std::move(result));
        // Breaks the cycle through the cancel callback, which holds the lookup.
        next->registration.reset();
        {
            const std::lock_guard lock(queue->mutex);
            --queue->delivering;
        }
        queue->changed.notify_all();
    }
}

}  // namespace

std::vector<IpAddress> addrinfo_addresses(const addrinfo* list) {
    std::vector<IpAddress> addresses;
    for (const addrinfo* entry = list; entry != nullptr; entry = entry->ai_next) {
        IpAddress address;
        if (entry->ai_family == AF_INET && entry->ai_addrlen >= sizeof(sockaddr_in)) {
            sockaddr_in v4{};
            std::memcpy(&v4, entry->ai_addr, sizeof v4);
            address.bytes[10] = 0xFF;
            address.bytes[11] = 0xFF;
            std::memcpy(address.bytes.data() + 12, &v4.sin_addr, 4);
        } else if (entry->ai_family == AF_INET6 && entry->ai_addrlen >= sizeof(sockaddr_in6)) {
            sockaddr_in6 v6{};
            std::memcpy(&v6, entry->ai_addr, sizeof v6);
            std::memcpy(address.bytes.data(), &v6.sin6_addr, 16);
        } else {
            continue;
        }
        if (std::ranges::find(addresses, address) == addresses.end()) addresses.push_back(address);
    }
    return addresses;
}

struct LinuxResolver::Impl {
    std::shared_ptr<Queue> queue = std::make_shared<Queue>();
    std::vector<std::thread> threads;
};

LinuxResolver::LinuxResolver() : impl_(std::make_unique<Impl>()) {
    for (std::size_t i = 0; i < kThreads; ++i) {
        impl_->threads.emplace_back([queue = impl_->queue] {
            try {
                serve(queue);
            } catch (...) {
                REBOOT_LOG_ERROR(Net, "internal.bug: a resolver thread failed");
            }
        });
    }
}

LinuxResolver::~LinuxResolver() {
    std::vector<std::shared_ptr<Lookup>> abandoned;
    {
        std::unique_lock lock(impl_->queue->mutex);
        impl_->queue->stopping = true;
        abandoned.assign(impl_->queue->pending.begin(), impl_->queue->pending.end());
        impl_->queue->pending.clear();
        abandoned.insert(abandoned.end(), impl_->queue->running.begin(), impl_->queue->running.end());
        impl_->queue->changed.notify_all();
        // No `done` may run once the resolver is gone.
        impl_->queue->changed.wait(lock, [&] { return impl_->queue->delivering == 0; });
    }
    for (const std::shared_ptr<Lookup>& pending : abandoned) {
        pending->finish(std::unexpected(cancelled(pending->host)));
        pending->registration.reset();
    }
    // A thread inside getaddrinfo cannot be interrupted; it drops its result and ends, holding only the queue.
    for (std::thread& thread : impl_->threads) thread.detach();
}

void LinuxResolver::resolve(std::string host, CancelToken token,
                            UniqueFunction<void(Result<std::vector<IpAddress>>)> done) {
    auto pending = std::make_shared<Lookup>();
    pending->host = std::move(host);
    pending->done = std::move(done);
    pending->registration = token.on_cancel([pending](CancelReason) {
        pending->finish(std::unexpected(cancelled(pending->host)));
    });
    if (pending->finished) {
        pending->registration.reset();
        return;
    }
    {
        const std::lock_guard lock(impl_->queue->mutex);
        impl_->queue->pending.push_back(pending);
    }
    impl_->queue->changed.notify_one();
}

}  // namespace reboot::os_linux::platform
