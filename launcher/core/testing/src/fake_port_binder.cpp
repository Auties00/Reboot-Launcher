#include "reboot/testing/port_binder.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "reboot/foundation/net_types.hpp"
#include "reboot/testing/fake_loopback_peer_inspector.hpp"
#include "reboot/testing/fake_port_inspector.hpp"

namespace rb::testing {
namespace {

constexpr u32 kLoopback = 0x7F000001;

class FakePortBinder final : public IPortBinder {
public:
    FakePortBinder(FakePortInspector& ports, FakeLoopbackPeerInspector& peers, u32 pid, u32 uid)
        : ports_(ports), peers_(peers), pid_(pid), uid_(uid) {}

    ~FakePortBinder() override {
        for (const Endpoint& endpoint : tcp_) ports_.clear_tcp(endpoint);
        for (const Port port : udp_) ports_.clear_udp(port);
    }

    FakePortBinder(const FakePortBinder&) = delete;
    FakePortBinder& operator=(const FakePortBinder&) = delete;

    Result<Endpoint> bind_tcp() override {
        const Endpoint endpoint{IpAddress::v4(kLoopback), next_port()};
        ports_.set_tcp_owner(endpoint, {pid_, std::nullopt, false});
        tcp_.push_back(endpoint);
        return endpoint;
    }

    Result<Port> bind_udp() override {
        const Port port = next_port();
        ports_.set_udp_owner(port, {pid_, std::nullopt, false});
        udp_.push_back(port);
        return port;
    }

    Result<std::pair<Endpoint, Endpoint>> connect_loopback() override {
        const Endpoint ours{IpAddress::v4(kLoopback), next_port()};
        const Endpoint theirs{IpAddress::v4(kLoopback), next_port()};
        peers_.set_peer_uid(ours, theirs, uid_);
        return std::pair{ours, theirs};
    }

    void release(Port port) override {
        std::erase_if(tcp_, [&](const Endpoint& endpoint) {
            if (endpoint.port != port) return false;
            ports_.clear_tcp(endpoint);
            return true;
        });
        if (std::erase(udp_, port) > 0) ports_.clear_udp(port);
    }

    [[nodiscard]] u32 owner_pid() const override { return pid_; }

private:
    Port next_port() { return Port{next_port_++}; }

    FakePortInspector& ports_;
    FakeLoopbackPeerInspector& peers_;
    u32 pid_;
    u32 uid_;
    u16 next_port_ = 40000;
    std::vector<Endpoint> tcp_;
    std::vector<Port> udp_;
};

}  // namespace

std::unique_ptr<IPortBinder> make_fake_port_binder(FakePortInspector& ports, FakeLoopbackPeerInspector& peers, u32 pid,
                                                   u32 uid) {
    return std::make_unique<FakePortBinder>(ports, peers, pid, uid);
}

}  // namespace rb::testing
