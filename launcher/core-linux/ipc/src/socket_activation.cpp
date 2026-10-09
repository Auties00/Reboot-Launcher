#include "socket_activation.hpp"

#include <charconv>
#include <system_error>

namespace reboot::os_linux::ipc {
namespace {

[[nodiscard]] std::optional<u32> parse_decimal(std::string_view text) noexcept {
    if (text.empty() || text.front() < '0' || text.front() > '9') return std::nullopt;
    u32 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

}  // namespace

SocketActivation read_socket_activation(const ListenEnvironment& environment, u32 pid) noexcept {
    if (!environment.listen_pid) return {};
    const std::optional<u32> listen_pid = parse_decimal(*environment.listen_pid);
    if (!listen_pid || *listen_pid != pid) return {};
    return {true, environment.listen_fds ? parse_decimal(*environment.listen_fds) : std::nullopt};
}

}  // namespace reboot::os_linux::ipc
