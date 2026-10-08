#pragma once

#include <memory>
#include <string>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/ports/net.hpp"

namespace reboot::os_macos::platform {

// Covers no capability ids; IResolver over DNSServiceGetAddrInfo on one serial dispatch queue.
class MacResolver final : public ports::IResolver {
public:
    MacResolver();
    ~MacResolver() override;
    MacResolver(const MacResolver&) = delete;
    MacResolver& operator=(const MacResolver&) = delete;

    // Cancelling deallocates the query on the queue, so `done` still runs exactly once, there.
    void resolve(std::string host, CancelToken token,
                 UniqueFunction<void(Result<std::vector<IpAddress>>)> done) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::os_macos::platform
