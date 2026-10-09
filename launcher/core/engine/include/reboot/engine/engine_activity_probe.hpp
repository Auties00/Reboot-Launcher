#pragma once

#include <cstddef>
#include <memory>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/flat_map.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/updates/activity_probe.hpp"

namespace rb::sessions {
class SessionRegistry;
}

namespace rb::backend {
class BackendService;
}

namespace rb::publish {
class HostPublisher;
}

namespace rb::engine {

// Capabilities: none. Strand-only; the one view of live work, whose single listener is EngineLifecycle.
class EngineActivityProbe final : public updates::IActivityProbe {
public:
    EngineActivityProbe(const sessions::SessionRegistry& sessions, const backend::BackendService& backend,
                        const publish::HostPublisher& publisher, const OpRegistry& ops, EventBus& events);
    ~EngineActivityProbe() override;
    EngineActivityProbe(const EngineActivityProbe&) = delete;
    EngineActivityProbe& operator=(const EngineActivityProbe&) = delete;

    // A DetachedOp entry has no op id: OpRegistry only says whether one is live.
    [[nodiscard]] updates::ActivitySnapshot snapshot() const override;
    void set_on_change(UniqueFunction<void()> on_change) override;

    // An op start publishes no event, so ApiRouter calls this after one.
    void notify_changed();

    void on_connected(ConnectionId connection, contracts::ipc::ClientKind kind);
    void on_disconnected(ConnectionId connection);
    [[nodiscard]] std::size_t connections() const noexcept { return clients_.size(); }

private:
    const sessions::SessionRegistry& sessions_;
    const backend::BackendService& backend_;
    const publish::HostPublisher& publisher_;
    const OpRegistry& ops_;
    std::shared_ptr<Subscription> watch_;
    FlatMap<ConnectionId, contracts::ipc::ClientKind> clients_;
    UniqueFunction<void()> on_change_;
};

}  // namespace rb::engine
