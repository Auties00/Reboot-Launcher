#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "reboot/backend/backend_account.hpp"
#include "reboot/contracts/backend.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::backend {

// Times before the epoch clamp to 0, which the contract reads as "never".
[[nodiscard]] inline u64 to_unix_ms(std::chrono::system_clock::time_point at) noexcept {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()).count();
    return ms > 0 ? static_cast<u64>(ms) : 0;
}

[[nodiscard]] inline std::chrono::system_clock::time_point from_unix_ms(u64 ms) noexcept {
    return std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::milliseconds{ms})};
}

[[nodiscard]] inline BackendAccount to_account(const contracts::backend::AccountSummary& summary) {
    BackendAccount account;
    account.account_id = summary.account_id;
    account.kind = summary.record_id ? BackendAccountKind::Local : BackendAccountKind::Remote;
    if (summary.record_id) account.record = AccountRecordId{*summary.record_id};
    account.role = summary.role;
    account.display_name = summary.display_name;
    if (summary.last_login_unix_ms != 0) account.last_login = from_unix_ms(summary.last_login_unix_ms);
    return account;
}

// "host:port", with an IPv6 literal bracketed; the port is left out when unset.
[[nodiscard]] inline std::string host_port_text(const HostPort& endpoint) {
    std::string text = endpoint.host.find(':') == std::string::npos ? endpoint.host : "[" + endpoint.host + "]";
    if (endpoint.port) text += ":" + std::to_string(endpoint.port->value);
    return text;
}

}  // namespace rb::backend
