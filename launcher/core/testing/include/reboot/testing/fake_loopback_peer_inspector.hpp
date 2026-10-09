#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/net.hpp"

namespace rb::testing {

// Covers no capability ids (decision testing-strategy).
// ILoopbackPeerInspector for the front's ticket swap: unsupported (Windows and macOS shapes) every
// call fails with platform.not_supported; supported, it answers the uid set for the connection.
class FakeLoopbackPeerInspector final : public ports::ILoopbackPeerInspector {
public:
    explicit FakeLoopbackPeerInspector(bool supported) : supported_(supported) {}

    Result<std::optional<u32>> peer_uid(Endpoint local, Endpoint remote) override;

    void set_peer_uid(Endpoint local, Endpoint remote, u32 uid);
    void set_supported(bool supported);

private:
    mutable std::mutex mutex_;
    bool supported_;
    std::map<std::pair<Endpoint, Endpoint>, u32> uids_;
};

}  // namespace rb::testing
