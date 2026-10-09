#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>

// Asio first: its socket_ops wait on libnatpmp's socket portably, and on Windows it starts Winsock.
#include <boost/asio/detail/socket_ops.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <natpmp.h>

#include "asio_endpoint.hpp"
#include "gateway_codes.hpp"
#include "reboot/net/port_mapping_gateway.hpp"

namespace reboot::net {

namespace {

namespace asio = boost::asio;

// Waits in slices this long, so a cancelled discovery stops promptly.
constexpr int kWaitSliceMs = 250;

// libnatpmp: the default gateway, NAT-PMP requests resent on its own schedule.
class NatpmpGateway final : public IPortMappingGateway {
public:
    NatpmpGateway() = default;
    NatpmpGateway(const NatpmpGateway&) = delete;
    NatpmpGateway& operator=(const NatpmpGateway&) = delete;
    ~NatpmpGateway() override { close(); }

    [[nodiscard]] MappingMethod method() const noexcept override { return MappingMethod::NatPmp; }

    std::expected<GatewayInfo, GatewayError> discover(std::chrono::milliseconds timeout, const CancelToken& token) override {
        const std::scoped_lock lock(mutex_);
        close();
        if (auto opened = open(); !opened) return std::unexpected(opened.error());
        if (const int sent = sendpublicaddressrequest(&natpmp_); sent < 0) return std::unexpected(fail(sent));
        natpmpresp_t response{};
        if (auto answered = wait(timeout, token, response); !answered) {
            close();
            // No answer at all means no NAT-PMP gateway.
            if (answered.error().code == GatewayErrorCode::Timeout) return std::unexpected(GatewayError{GatewayErrorCode::NoGateway});
            return std::unexpected(answered.error());
        }
        IpAddress external;
        std::array<u8, 4> bytes{};
        std::memcpy(bytes.data(), &response.pnu.publicaddress.addr, 4);
        external = IpAddress::v4(u32{bytes[0]} << 24 | u32{bytes[1]} << 16 | u32{bytes[2]} << 8 | u32{bytes[3]});
        return GatewayInfo{lan_, external};
    }

    std::expected<PortMapping, GatewayError> add(const GatewayMappingRequest& request,
                                                 std::chrono::milliseconds timeout) override {
        const std::scoped_lock lock(mutex_);
        if (auto opened = open(); !opened) return std::unexpected(opened.error());
        // NAT-PMP reads a zero lifetime as a delete, and it has no permanent entries.
        const auto lease = request.lease > std::chrono::seconds::zero() ? request.lease : std::chrono::seconds{3600};
        const int sent = sendnewportmappingrequest(&natpmp_, NATPMP_PROTOCOL_UDP, request.internal.value, request.external.value,
                                                   static_cast<uint32_t>(lease.count()));
        if (sent < 0) return std::unexpected(fail(sent));
        natpmpresp_t response{};
        if (auto answered = wait(timeout, CancelToken{}, response); !answered) return std::unexpected(answered.error());
        return PortMapping{request.internal, Port{response.pnu.newportmapping.mappedpublicport}, MappingMethod::NatPmp,
                           std::chrono::seconds{response.pnu.newportmapping.lifetime}, lan_};
    }

    std::expected<void, GatewayError> remove(const PortMapping& mapping, std::chrono::milliseconds timeout) override {
        const std::scoped_lock lock(mutex_);
        if (auto opened = open(); !opened) return std::unexpected(opened.error());
        const int sent = sendnewportmappingrequest(&natpmp_, NATPMP_PROTOCOL_UDP, mapping.internal.value, 0, 0);
        if (sent < 0) return std::unexpected(fail(sent));
        natpmpresp_t response{};
        if (auto answered = wait(timeout, CancelToken{}, response); !answered) return std::unexpected(answered.error());
        return {};
    }

    std::expected<std::vector<GatewayEntry>, GatewayError> list(std::chrono::milliseconds) override {
        return std::unexpected(GatewayError{GatewayErrorCode::Unsupported, std::nullopt, "NAT-PMP cannot list mappings"});
    }

private:
    std::expected<void, GatewayError> open() {
        if (open_) return {};
        if (const int result = initnatpmp(&natpmp_, 0, 0); result < 0) return std::unexpected(natpmp_error(result));
        open_ = true;
        // The LAN address is the one our traffic to the gateway leaves from; connecting sends nothing.
        std::array<u8, 4> gateway{};
        std::memcpy(gateway.data(), &natpmp_.gateway, 4);
        boost::system::error_code error;
        asio::ip::udp::socket probe(io_);
        probe.connect(asio::ip::udp::endpoint(asio::ip::address_v4(gateway), NATPMP_PORT), error);
        if (!error) {
            const asio::ip::udp::endpoint local = probe.local_endpoint(error);
            if (!error) lan_ = from_asio(local.address());
        }
        if (error) {
            close();
            return std::unexpected(GatewayError{GatewayErrorCode::Failed, error.value(), "no LAN address towards the gateway"});
        }
        return {};
    }

    void close() {
        if (!open_) return;
        closenatpmp(&natpmp_);
        natpmp_ = {};
        open_ = false;
    }

    GatewayError fail(int code) {
        close();
        return natpmp_error(code);
    }

    std::expected<void, GatewayError> wait(std::chrono::milliseconds timeout, const CancelToken& token, natpmpresp_t& response) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            const int result = readnatpmpresponseorretry(&natpmp_, &response);
            if (result >= 0) return {};
            if (result != NATPMP_TRYAGAIN) return std::unexpected(fail(result));
            if (token.cancelled()) return std::unexpected(GatewayError{GatewayErrorCode::Failed, std::nullopt, "cancelled"});
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left <= std::chrono::milliseconds::zero()) return std::unexpected(GatewayError{GatewayErrorCode::Timeout});
            timeval retry{};
            int slice = kWaitSliceMs;
            if (getnatpmprequesttimeout(&natpmp_, &retry) == 0)
                slice = static_cast<int>(std::max<long long>(1, static_cast<long long>(retry.tv_sec) * 1000 + retry.tv_usec / 1000));
            slice = std::min({slice, kWaitSliceMs, static_cast<int>(left.count())});
            boost::system::error_code ignored;
            asio::detail::socket_ops::poll_read(static_cast<asio::detail::socket_type>(natpmp_.s), 0, slice, ignored);
        }
    }

    std::mutex mutex_;
    asio::io_context io_;
    natpmp_t natpmp_{};
    bool open_ = false;
    IpAddress lan_;
};

}  // namespace

std::unique_ptr<IPortMappingGateway> make_natpmp_gateway() { return std::make_unique<NatpmpGateway>(); }

}  // namespace reboot::net
