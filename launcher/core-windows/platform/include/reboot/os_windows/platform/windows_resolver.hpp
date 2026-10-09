#pragma once

#include <string>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/ports/net.hpp"

namespace rb::os_windows::platform {

// Covers no capability ids; IResolver over overlapped GetAddrInfoExW, which GetAddrInfoExCancel can stop.
class WindowsResolver final : public ports::IResolver {
public:
    // `done` runs once, on the system thread that completes the query.
    void resolve(std::string host, CancelToken token, UniqueFunction<void(Result<std::vector<IpAddress>>)> done) override;
};

}  // namespace rb::os_windows::platform
