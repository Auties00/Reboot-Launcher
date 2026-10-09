#include "reboot/engine/engine_activity_probe.hpp"

#include <limits>
#include <utility>
#include <vector>

#include "reboot/backend/backend_service.hpp"
#include "reboot/backend/backend_state.hpp"
#include "reboot/publish/host_publisher.hpp"
#include "reboot/sessions/session_info.hpp"
#include "reboot/sessions/session_phase.hpp"
#include "reboot/sessions/session_registry.hpp"

namespace reboot::engine {

namespace {

// What can end live work; the queue only wakes the probe, it never holds anything.
constexpr std::size_t kWatchBudget = std::size_t{64} << 10;

}  // namespace

EngineActivityProbe::EngineActivityProbe(const sessions::SessionRegistry& sessions,
                                         const backend::BackendService& backend,
                                         const publish::HostPublisher& publisher, const OpRegistry& ops,
                                         EventBus& events)
    : sessions_(sessions), backend_(backend), publisher_(publisher), ops_(ops) {
    watch_ = events.subscribe(EventFilter{{EventKind::SessionStateChanged, EventKind::SessionEnded,
                                           EventKind::BackendStateChanged, EventKind::PublishStateChanged,
                                           EventKind::OpCompleted},
                                          std::nullopt,
                                          std::nullopt},
                              kWatchBudget);
    watch_->set_notify([this] {
        std::vector<EventEnvelope> drained;
        watch_->drain(drained, std::numeric_limits<std::size_t>::max());
        watch_->take_resync();
        notify_changed();
    });
}

EngineActivityProbe::~EngineActivityProbe() { watch_->set_notify({}); }

updates::ActivitySnapshot EngineActivityProbe::snapshot() const {
    updates::ActivitySnapshot snapshot;
    for (const sessions::SessionInfo& session : sessions_.list()) {
        if (!sessions::is_live(session.phase)) continue;
        updates::LiveActivity activity;
        activity.kind = session.kind == sessions::SessionKind::Play ? updates::LiveKind::PlaySession
                                                                     : updates::LiveKind::HostSession;
        activity.session = session.id;
        activity.host_profile = session.profile;
        activity.payload_version = session.pinned.payload_version;
        activity.runtime_id = session.pinned.runtime_id;
        snapshot.live.push_back(std::move(activity));
    }
    if (backend_.state().pinned) snapshot.live.push_back(updates::LiveActivity{.kind = updates::LiveKind::BackendPin});
    if (publisher_.has_publications())
        snapshot.live.push_back(updates::LiveActivity{.kind = updates::LiveKind::Publication});
    if (ops_.has_live_detached()) snapshot.live.push_back(updates::LiveActivity{.kind = updates::LiveKind::DetachedOp});
    for (const auto& [connection, kind] : clients_) snapshot.connected_clients.push_back(kind);
    return snapshot;
}

void EngineActivityProbe::set_on_change(UniqueFunction<void()> on_change) { on_change_ = std::move(on_change); }

void EngineActivityProbe::notify_changed() {
    if (on_change_) on_change_();
}

void EngineActivityProbe::on_connected(ConnectionId connection, contracts::ipc::ClientKind kind) {
    clients_.insert_or_assign(connection, kind);
}

void EngineActivityProbe::on_disconnected(ConnectionId connection) { clients_.erase(connection); }

}  // namespace reboot::engine
