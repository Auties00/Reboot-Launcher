#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "reboot/foundation/executor.hpp"
#include "reboot/ports/ipc.hpp"

namespace reboot::testing {

// Covers no capability ids (decisions testing-strategy, async-event-model).
// The engine's IIpcListener and the client's IIpcConnector over one in-process endpoint namespace,
// keeping the real adapters' squatting and same-user rules; nothing listening fails EngineUnavailable.
class InMemoryIpc {
public:
    InMemoryIpc(Executor& deliver_on, ports::PeerIdentity self);
    ~InMemoryIpc();
    InMemoryIpc(const InMemoryIpc&) = delete;
    InMemoryIpc& operator=(const InMemoryIpc&) = delete;

    // Both stay valid after the InMemoryIpc is gone; they then see an empty namespace.
    [[nodiscard]] std::unique_ptr<ports::IIpcListener> make_listener();
    [[nodiscard]] std::unique_ptr<ports::IIpcConnector> make_connector();

    // Connections made from now on present `identity` to the listener.
    void set_client_identity(ports::PeerIdentity identity);
    // Takes `endpoint_name` for `owner` before the engine does.
    void squat(std::string endpoint_name, ports::PeerIdentity owner);
    // Breaks every open connection, as an engine crash does.
    void sever_all();

    [[nodiscard]] bool listening(std::string_view endpoint_name) const;
    [[nodiscard]] std::size_t open_connections() const;
    [[nodiscard]] const ports::PeerIdentity& self() const noexcept;

private:
    struct Hub;
    // Shared with every listener, connector and stream it made.
    std::shared_ptr<Hub> hub_;
};

}  // namespace reboot::testing
