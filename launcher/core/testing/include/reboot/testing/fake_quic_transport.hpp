#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/net.hpp"
#include "reboot/testing/fake_quic_peer.hpp"

namespace rb::testing {

// Covers no capability ids (decision testing-strategy).
// IQuicTransport for browser and publish tests; each connection waits for the test to accept or
// refuse it. Tests that need a real rbsb edge run the server-browser edge over MsQuic instead.
class FakeQuicTransport final : public ports::IQuicTransport {
public:
    explicit FakeQuicTransport(Executor& deliver_on) : deliver_on_(deliver_on) {}

    Result<std::unique_ptr<ports::IQuicConnection>> open_connection(const ports::QuicConnectOptions& options,
                                                                     ports::QuicCallbacks callbacks) override;

    // Runs for each new connection before any callback, e.g. to accept it at once.
    void on_connection(UniqueFunction<void(FakeQuicPeer&)> handler);
    void fail_next_open(Diagnostic error);

    [[nodiscard]] std::vector<FakeQuicPeer*> connections() const;
    [[nodiscard]] FakeQuicPeer* last() const;

private:
    Executor& deliver_on_;
    std::vector<std::unique_ptr<FakeQuicPeer>> connections_;
    UniqueFunction<void(FakeQuicPeer&)> on_connection_;
    std::optional<Diagnostic> next_open_error_;
};

}  // namespace rb::testing
