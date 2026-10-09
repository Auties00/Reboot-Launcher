#include "darwin.hpp"

#include "reboot/os_macos/platform/mac_prerequisite_probe.hpp"

#include <net/route.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <utility>

#include "apple_shims.hpp"
#include "firewall_output.hpp"
#include "macos_version.hpp"
#include "messages.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "run_program.hpp"
#include "sysctl_value.hpp"

namespace rb::os_macos::platform {

namespace {

constexpr std::string_view kSocketFilter = "/usr/libexec/ApplicationFirewall/socketfilterfw";
constexpr std::string_view kSoftwareUpdate = "/usr/sbin/softwareupdate";
constexpr std::string_view kRosettaRuntime = "/Library/Apple/usr/libexec/oah/libRosettaRuntime";
constexpr std::string_view kFirewallPane = "x-apple.systempreferences:com.apple.preference.security?Firewall";
constexpr std::string_view kLocalNetworkPane = "x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork";
constexpr std::chrono::seconds kFirewallQueryDeadline{5};
// Rosetta is a download of a few hundred megabytes.
constexpr std::chrono::minutes kRosettaInstallDeadline{15};
// The discard service: the datagram only has to leave this process.
constexpr u16 kDiscardPort = 9;

[[nodiscard]] ports::PrerequisiteStatus status(std::string_view id, bool met, MessageId remediation) {
    return ports::PrerequisiteStatus{
        .id = std::string(id), .met = met, .remediation_message_id = met ? std::nullopt : std::optional<MessageId>{remediation}};
}

[[nodiscard]] bool rosetta_installed() {
    if (!apple_silicon()) return true;
    struct stat info {};
    return ::stat(std::string(kRosettaRuntime).c_str(), &info) == 0;
}

[[nodiscard]] std::optional<std::string> socket_filter(std::vector<std::string> args) {
    Result<ProgramResult> ran = run_program(NativePath{std::string(kSocketFilter)}, std::move(args), kFirewallQueryDeadline);
    if (!ran || ran->code != 0) return std::nullopt;
    return std::move(ran->output);
}

// An unreadable firewall state counts as not blocking: a failed check must not stop hosting.
[[nodiscard]] bool firewall_blocks_game_server(const std::optional<NativePath>& game_server_exe) {
    const std::optional<std::string> global = socket_filter({"--getglobalstate"});
    if (!global || !firewall_enabled(*global).value_or(false)) return false;
    if (const std::optional<std::string> all = socket_filter({"--getblockall"}); all && firewall_blocks_all(*all).value_or(false))
        return true;
    if (!game_server_exe) return false;
    const std::optional<std::string> app = socket_filter({"--getappblocked", game_server_exe->string()});
    return app && firewall_blocks_app(*app);
}

// sa_len rounded up to the routing socket's 4-byte alignment; an empty address still takes 4.
[[nodiscard]] std::size_t sockaddr_space(u8 length) noexcept {
    constexpr std::size_t kAlign = sizeof(u32);
    return length == 0 ? kAlign : (static_cast<std::size_t>(length) + kAlign - 1) / kAlign * kAlign;
}

// The IPv4 default route's gateway, from the kernel routing table.
[[nodiscard]] std::optional<sockaddr_in> default_gateway() {
    std::array<int, 6> mib{CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_GATEWAY};
    std::size_t size = 0;
    if (::sysctl(mib.data(), static_cast<u_int>(mib.size()), nullptr, &size, nullptr, 0) != 0 || size == 0) return std::nullopt;
    std::vector<u8> table(size);
    if (::sysctl(mib.data(), static_cast<u_int>(mib.size()), table.data(), &size, nullptr, 0) != 0) return std::nullopt;
    table.resize(size);

    std::size_t offset = 0;
    while (offset + sizeof(rt_msghdr) <= table.size()) {
        rt_msghdr header{};
        std::memcpy(&header, table.data() + offset, sizeof header);
        if (header.rtm_msglen == 0 || offset + header.rtm_msglen > table.size()) break;
        const std::size_t end = offset + header.rtm_msglen;
        std::size_t cursor = offset + sizeof header;
        std::optional<sockaddr_in> destination;
        std::optional<sockaddr_in> gateway;
        bool netmask_set = false;
        for (int index = 0; index < RTAX_MAX && cursor < end; ++index) {
            if ((header.rtm_addrs & (1 << index)) == 0) continue;
            sockaddr address{};
            std::memcpy(&address, table.data() + cursor, std::min(sizeof address, end - cursor));
            if ((index == RTAX_DST || index == RTAX_GATEWAY) && address.sa_family == AF_INET &&
                cursor + sizeof(sockaddr_in) <= end) {
                sockaddr_in inet{};
                std::memcpy(&inet, table.data() + cursor, sizeof inet);
                (index == RTAX_DST ? destination : gateway) = inet;
            }
            // A default route's netmask is empty or all zero.
            if (index == RTAX_NETMASK && address.sa_len > 2) {
                for (std::size_t byte = cursor + 2; byte < cursor + address.sa_len && byte < end; ++byte)
                    if (table[byte] != 0) netmask_set = true;
            }
            cursor += sockaddr_space(address.sa_len);
        }
        if (destination && gateway && destination->sin_addr.s_addr == 0 && !netmask_set) return gateway;
        offset = end;
    }
    return std::nullopt;
}

}  // namespace

MacPrerequisiteProbe::MacPrerequisiteProbe(std::optional<NativePath> game_server_exe)
    : game_server_exe_(std::move(game_server_exe)) {}

std::vector<ports::PrerequisiteStatus> MacPrerequisiteProbe::check() {
    const std::optional<u32> major = macos_major_version(sysctl_string("kern.osproductversion").value_or(""));
    return {
        status(kAppleSiliconId, apple_silicon(), kNeedsAppleSilicon),
        status(kMinimumVersionId, major.value_or(0) >= kMinimumMajorVersion, kMacosTooOld),
        status(kRosettaId, rosetta_installed(), kRosettaMissing),
        status(kMetal3Id, shims::metal3_supported(), kMetal3Missing),
        status(kAppFirewallId, !firewall_blocks_game_server(game_server_exe_), kFirewallBlocksGameServer),
        status(kLocalNetworkId, !local_network_denied_, kLocalNetworkDenied),
    };
}

Result<void> MacPrerequisiteProbe::remediate(std::string_view id) {
    if (id == kRosettaId) {
        if (rosetta_installed()) return {};
        Result<ProgramResult> installed = run_program(NativePath{std::string(kSoftwareUpdate)},
                                                      {"--install-rosetta", "--agree-to-license"}, kRosettaInstallDeadline);
        if (!installed) return std::unexpected(std::move(installed.error()));
        if (installed->code == 0 && rosetta_installed()) return {};
        const i64 exit_code = installed->code ? *installed->code : 128 + installed->signal.value_or(0);
        return make_diag(ErrorDomain::Platform, kRosettaInstallFailed)
            .arg("exit_code", exit_code)
            .detail(installed->output)
            .retryable()
            .fail();
    }
    if (id == kAppFirewallId) return shims::workspace_open(kFirewallPane);
    if (id == kLocalNetworkId) {
        const std::optional<sockaddr_in> gateway = default_gateway();
        if (!gateway) return make_diag(ErrorDomain::Platform, kNoGateway).retryable().fail();
        sockaddr_in target = *gateway;
        target.sin_port = std::endian::native == std::endian::little ? std::byteswap(kDiscardPort) : kDiscardPort;
        posix::UniqueFd sender{::socket(AF_INET, SOCK_DGRAM, 0)};
        if (!sender.valid()) return std::unexpected(posix::call_failed("socket", errno));
        const u8 probe = 0;
        sockaddr address{};
        std::memcpy(&address, &target, sizeof address);
        const bool sent = ::sendto(sender.get(), &probe, sizeof probe, 0, &address, sizeof target) >= 0;
        const int error = errno;
        const bool first = !local_network_prompted_;
        local_network_prompted_ = true;
        if (sent) {
            local_network_denied_ = false;
            return {};
        }
        if (error != EHOSTUNREACH) return std::unexpected(posix::call_failed("sendto", error));
        // The first send only raises the prompt; a refusal after it means access is off.
        if (first) return {};
        local_network_denied_ = true;
        (void)shims::workspace_open(kLocalNetworkPane);
        return make_diag(ErrorDomain::Platform, kLocalNetworkDenied).fail();
    }
    const bool known = id == kAppleSiliconId || id == kMinimumVersionId || id == kMetal3Id;
    return make_diag(ErrorDomain::Platform, kNoRemediation)
        .arg("id", id)
        .kind(known ? ErrorKind::Unsupported : ErrorKind::InvalidInput)
        .fail();
}

}  // namespace rb::os_macos::platform
