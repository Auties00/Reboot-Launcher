#include <algorithm>
#include <array>
#include <charconv>
#include <mutex>
#include <string>
#include <string_view>

// Asio first: on Windows it brings in Winsock and starts it, which miniupnpc needs.
#include <boost/asio/ip/udp.hpp>
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>

#include "gateway_codes.hpp"
#include "reboot/net/port_mapping_gateway.hpp"

namespace rb::net {

namespace {

// UPnP 714 NoSuchEntryInArray and 713 SpecifiedArrayIndexInvalid.
constexpr int kNoSuchEntry = 714;
constexpr int kIndexInvalid = 713;
// A router listing more than this is broken; the sweep stops there.
constexpr int kMaxListedEntries = 1024;

[[nodiscard]] GatewayError with_detail(int code) {
    GatewayError error = upnp_error(code);
    if (const char* text = strupnperror(code)) error.detail = text;
    return error;
}

[[nodiscard]] std::optional<Port> port_of(const char* text) {
    const std::string_view view(text);
    u16 value = 0;
    const auto [end, ec] = std::from_chars(view.data(), view.data() + view.size(), value);
    if (ec != std::errc{} || end != view.data() + view.size() || value == 0) return std::nullopt;
    return Port{value};
}

// miniupnpc: SSDP discovery, then SOAP calls to the first valid IGD.
class MiniupnpcGateway final : public IPortMappingGateway {
public:
    MiniupnpcGateway() = default;
    MiniupnpcGateway(const MiniupnpcGateway&) = delete;
    MiniupnpcGateway& operator=(const MiniupnpcGateway&) = delete;
    ~MiniupnpcGateway() override { forget(); }

    [[nodiscard]] MappingMethod method() const noexcept override { return MappingMethod::Upnp; }

    std::expected<GatewayInfo, GatewayError> discover(std::chrono::milliseconds timeout, const CancelToken& token) override {
        const std::scoped_lock lock(mutex_);
        return discover_locked(timeout, token);
    }

    std::expected<PortMapping, GatewayError> add(const GatewayMappingRequest& request,
                                                 std::chrono::milliseconds timeout) override {
        const std::scoped_lock lock(mutex_);
        if (auto ready = ensure(timeout); !ready) return std::unexpected(ready.error());
        const std::string external = std::to_string(request.external.value);
        const std::string internal = std::to_string(request.internal.value);
        const std::string client = request.lan_address.to_string();
        const std::string lease = std::to_string(request.lease.count());
        const int result = UPNP_AddPortMapping(urls_.controlURL, data_.first.servicetype, external.c_str(), internal.c_str(),
                                               client.c_str(), request.description.c_str(), "UDP", nullptr, lease.c_str());
        if (result != UPNPCOMMAND_SUCCESS) return std::unexpected(with_detail(result));
        return PortMapping{request.internal, request.external, MappingMethod::Upnp, request.lease, request.lan_address};
    }

    std::expected<void, GatewayError> remove(const PortMapping& mapping, std::chrono::milliseconds timeout) override {
        const std::scoped_lock lock(mutex_);
        if (auto ready = ensure(timeout); !ready) return std::unexpected(ready.error());
        const std::string external = std::to_string(mapping.external.value);
        const int result = UPNP_DeletePortMapping(urls_.controlURL, data_.first.servicetype, external.c_str(), "UDP", nullptr);
        if (result != UPNPCOMMAND_SUCCESS && result != kNoSuchEntry) return std::unexpected(with_detail(result));
        return {};
    }

    std::expected<std::vector<GatewayEntry>, GatewayError> list(std::chrono::milliseconds timeout) override {
        const std::scoped_lock lock(mutex_);
        if (auto ready = ensure(timeout); !ready) return std::unexpected(ready.error());
        std::vector<GatewayEntry> out;
        for (int index = 0; index < kMaxListedEntries; ++index) {
            const std::string index_text = std::to_string(index);
            std::array<char, 6> external{};
            std::array<char, 16> client{};
            std::array<char, 6> internal{};
            std::array<char, 4> protocol{};
            std::array<char, 80> description{};
            std::array<char, 4> enabled{};
            std::array<char, 64> remote_host{};
            std::array<char, 16> duration{};
            const int result = UPNP_GetGenericPortMappingEntry(
                urls_.controlURL, data_.first.servicetype, index_text.c_str(), external.data(), client.data(),
                internal.data(), protocol.data(), description.data(), enabled.data(), remote_host.data(), duration.data());
            if (result == kIndexInvalid || (result != UPNPCOMMAND_SUCCESS && index > 0)) break;
            if (result != UPNPCOMMAND_SUCCESS) return std::unexpected(with_detail(result));
            if (std::string_view(protocol.data()) != "UDP") continue;
            const std::optional<Port> external_port = port_of(external.data());
            const std::optional<Port> internal_port = port_of(internal.data());
            const std::optional<IpAddress> address = IpAddress::parse(client.data());
            if (!external_port || !internal_port || !address) continue;
            out.push_back(GatewayEntry{*internal_port, *external_port, *address, description.data()});
        }
        return out;
    }

private:
    std::expected<void, GatewayError> ensure(std::chrono::milliseconds timeout) {
        if (have_igd_) return {};
        if (auto found = discover_locked(timeout, CancelToken{}); !found) return std::unexpected(found.error());
        return {};
    }

    std::expected<GatewayInfo, GatewayError> discover_locked(std::chrono::milliseconds timeout, const CancelToken& token) {
        forget();
        int error = 0;
        const int delay = static_cast<int>(std::clamp<std::chrono::milliseconds::rep>(timeout.count(), 100, 60000));
        UPNPDev* devices = upnpDiscover(delay, nullptr, nullptr, UPNP_LOCAL_PORT_ANY, 0, 2, &error);
        if (devices == nullptr || token.cancelled()) {
            if (devices != nullptr) freeUPNPDevlist(devices);
            GatewayError failure{GatewayErrorCode::NoGateway};
            if (error != 0) failure.protocol_code = error;
            return std::unexpected(failure);
        }
        std::array<char, 64> lan{};
        std::array<char, 64> wan{};
        const int status = UPNP_GetValidIGD(devices, &urls_, &data_, lan.data(), static_cast<int>(lan.size()), wan.data(),
                                            static_cast<int>(wan.size()));
        freeUPNPDevlist(devices);
        if (status == UPNP_NO_IGD) return std::unexpected(GatewayError{GatewayErrorCode::NoGateway});
        if (status != UPNP_CONNECTED_IGD && status != UPNP_PRIVATEIP_IGD) {
            FreeUPNPUrls(&urls_);
            return std::unexpected(GatewayError{GatewayErrorCode::NoGateway, status, "the IGD is not connected"});
        }
        const std::optional<IpAddress> lan_address = IpAddress::parse(lan.data());
        if (!lan_address) {
            FreeUPNPUrls(&urls_);
            return std::unexpected(GatewayError{GatewayErrorCode::Failed, std::nullopt, "no LAN address towards the IGD"});
        }
        have_igd_ = true;
        GatewayInfo info{*lan_address, std::nullopt};
        std::array<char, 40> external{};
        if (UPNP_GetExternalIPAddress(urls_.controlURL, data_.first.servicetype, external.data()) == UPNPCOMMAND_SUCCESS)
            info.external_address = IpAddress::parse(external.data());
        return info;
    }

    void forget() {
        if (!have_igd_) return;
        FreeUPNPUrls(&urls_);
        urls_ = {};
        data_ = {};
        have_igd_ = false;
    }

    std::mutex mutex_;
    bool have_igd_ = false;
    UPNPUrls urls_{};
    IGDdatas data_{};
};

}  // namespace

std::unique_ptr<IPortMappingGateway> make_miniupnpc_gateway() { return std::make_unique<MiniupnpcGateway>(); }

}  // namespace rb::net
