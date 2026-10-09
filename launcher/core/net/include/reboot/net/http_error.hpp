#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::net {

enum class HttpErrorCode : u8 {
    InvalidUrl,
    Dns,
    Connect,
    ConnectTimeout,
    Tls,
    TotalTimeout,
    Stalled,
    ResponseTooLarge,
    PlainHttpNeedsConsent,
    DowngradeRefused,
    Transport,
    Cancelled,
};

// `limit` is the timeout or stall window that ran out; `byte_limit` the body cap. For InvalidUrl,
// `host` holds the rejected URL.
struct HttpError {
    HttpErrorCode code = HttpErrorCode::Transport;
    std::string host;
    std::optional<std::chrono::milliseconds> limit;
    std::optional<u64> byte_limit;
    std::optional<std::string> detail;
    std::optional<SystemError> os_error;
};

// Dns, Connect, ConnectTimeout, TotalTimeout, Stalled and Transport come out retryable.
[[nodiscard]] Diagnostic to_diagnostic(const HttpError& error);

}  // namespace reboot::net
