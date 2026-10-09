#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/windows_resolver.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>

#include "reboot/foundation/log.hpp"
#include "wide.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

namespace {

using Done = UniqueFunction<void(Result<std::vector<IpAddress>>)>;

[[nodiscard]] DWORD start_winsock() noexcept {
    static const DWORD started = [] {
        WSADATA data{};
        return static_cast<DWORD>(WSAStartup(MAKEWORD(2, 2), &data));
    }();
    return started;
}

[[nodiscard]] std::vector<IpAddress> addresses_of(const ADDRINFOEXW* list) {
    std::vector<IpAddress> addresses;
    for (const ADDRINFOEXW* info = list; info != nullptr; info = info->ai_next) {
        IpAddress address;
        if (info->ai_family == AF_INET && info->ai_addrlen >= sizeof(sockaddr_in)) {
            const auto* v4 = reinterpret_cast<const sockaddr_in*>(info->ai_addr);
            address = IpAddress::v4(ntohl(v4->sin_addr.s_addr));
        } else if (info->ai_family == AF_INET6 && info->ai_addrlen >= sizeof(sockaddr_in6)) {
            const auto* v6 = reinterpret_cast<const sockaddr_in6*>(info->ai_addr);
            std::memcpy(address.bytes.data(), &v6->sin6_addr, address.bytes.size());
        } else {
            continue;
        }
        if (std::ranges::find(addresses, address) == addresses.end()) addresses.push_back(address);
    }
    return addresses;
}

// One query in flight. It owns itself until its completion runs; the cancel callback holds it only
// weakly, and strongly while it runs.
class Lookup {
public:
    // OVERLAPPED first in a standard-layout struct, so the completion finds its lookup.
    struct Envelope {
        OVERLAPPED overlapped;
        Lookup* lookup;
    };

    Lookup(std::wstring host, CancelToken token, Done done)
        : host_(std::move(host)), token_(std::move(token)), done_(std::move(done)) {
        envelope_.overlapped = OVERLAPPED{};
        envelope_.lookup = this;
    }

    static void start(std::shared_ptr<Lookup> lookup) {
        Lookup& self = *lookup;
        ADDRINFOEXW hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        INT status = 0;
        {
            std::scoped_lock lock(self.mutex_);
            self.keep_alive_ = lookup;
            status = GetAddrInfoExW(self.host_.c_str(), nullptr, NS_DNS, nullptr, &hints, &self.result_, nullptr,
                                    &self.envelope_.overlapped, &Lookup::completed, &self.cancel_handle_);
        }
        // Anything but pending completed synchronously, without the completion routine.
        if (status != WSA_IO_PENDING) {
            self.finish(static_cast<DWORD>(status));
            return;
        }
        std::weak_ptr<Lookup> weak = lookup;
        CancelRegistration registration = self.token_.on_cancel([weak](CancelReason) {
            const std::shared_ptr<Lookup> live = weak.lock();
            if (!live) return;
            HANDLE handle = nullptr;
            {
                std::scoped_lock lock(live->mutex_);
                if (live->finished_) return;
                handle = live->cancel_handle_;
            }
            GetAddrInfoExCancel(&handle);
        });
        std::scoped_lock lock(self.mutex_);
        if (!self.finished_) self.registration_ = std::move(registration);
    }

private:
    static void CALLBACK completed(DWORD error, DWORD, LPWSAOVERLAPPED overlapped) {
        reinterpret_cast<Envelope*>(overlapped)->lookup->finish(error);
    }

    void finish(DWORD error) {
        // The registration stays: a cancel callback running elsewhere holds this lookup, so it is
        // reset there, never here while that callback may be inside GetAddrInfoExCancel.
        std::shared_ptr<Lookup> keep;
        Done done;
        {
            std::scoped_lock lock(mutex_);
            if (finished_) return;
            finished_ = true;
            keep = std::move(keep_alive_);
            done = std::move(done_);
        }
        Result<std::vector<IpAddress>> outcome = std::vector<IpAddress>{};
        if (error == 0) {
            outcome = addresses_of(result_);
            if (outcome->empty()) outcome = std::unexpected(call_failed("GetAddrInfoExW", WSANO_DATA));
        } else {
            Diagnostic failure = call_failed("GetAddrInfoExW", error);
            if (error == WSAHOST_NOT_FOUND || error == WSANO_DATA) failure.kind = ErrorKind::NotFound;
            if (error == WSA_E_CANCELLED || token_.cancelled()) failure.kind = ErrorKind::Cancelled;
            outcome = std::unexpected(std::move(failure));
        }
        if (result_ != nullptr) FreeAddrInfoExW(result_);
        result_ = nullptr;
        try {
            done(std::move(outcome));
        } catch (...) {
            REBOOT_LOG_ERROR(Net, "internal.bug: a resolver callback threw");
        }
    }

    Envelope envelope_{};
    std::wstring host_;
    CancelToken token_;
    Done done_;
    std::mutex mutex_;
    ADDRINFOEXW* result_ = nullptr;
    HANDLE cancel_handle_ = nullptr;
    bool finished_ = false;
    std::shared_ptr<Lookup> keep_alive_;
    CancelRegistration registration_;
};

}  // namespace

void WindowsResolver::resolve(std::string host, CancelToken token, UniqueFunction<void(Result<std::vector<IpAddress>>)> done) {
    if (token.cancelled()) {
        Diagnostic cancelled = call_failed("GetAddrInfoExW", WSA_E_CANCELLED);
        cancelled.kind = ErrorKind::Cancelled;
        done(std::unexpected(std::move(cancelled)));
        return;
    }
    if (const DWORD error = start_winsock(); error != 0) {
        done(std::unexpected(call_failed("WSAStartup", error)));
        return;
    }
    Lookup::start(std::make_shared<Lookup>(widen(host), std::move(token), std::move(done)));
}

}  // namespace rb::os_windows::platform
