#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/ports/net.hpp"

namespace rb::os_linux::platform {

// Covers no capability ids; IResolver over blocking getaddrinfo on kThreads private threads.
// A deliberate exception to the WorkerPool rule for blocking DNS: make_platform gets no
// WorkerPool, and a lookup stuck on a dead DNS server must not hold a pool worker.
class LinuxResolver final : public ports::IResolver {
public:
    inline static constexpr std::size_t kThreads = 4;

    LinuxResolver();
    ~LinuxResolver() override;
    LinuxResolver(const LinuxResolver&) = delete;
    LinuxResolver& operator=(const LinuxResolver&) = delete;

    // getaddrinfo(AF_UNSPEC, AI_ADDRCONFIG). getaddrinfo cannot be cancelled, so cancelling
    // `token` runs `done` at once on the cancelling thread and the lookup's result is dropped.
    // `done` runs exactly once; v4 results are IPv4-mapped and deduplicated in getaddrinfo's order.
    void resolve(std::string host, CancelToken token,
                 UniqueFunction<void(Result<std::vector<IpAddress>>)> done) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::os_linux::platform
