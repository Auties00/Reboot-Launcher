#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/net.hpp"

namespace rb::net {

enum class HttpMethod : u8 { Get, Head, Post, Put, Delete };

[[nodiscard]] constexpr bool is_idempotent(HttpMethod method) noexcept {
    return method == HttpMethod::Get || method == HttpMethod::Head;
}

enum class HttpKind : u8 { Small, Download };

[[nodiscard]] constexpr OpKind op_kind(HttpKind kind) noexcept {
    return kind == HttpKind::Download ? OpKind::HttpDownload : OpKind::HttpSmall;
}

inline constexpr std::chrono::milliseconds kHttpConnectTimeout = std::chrono::seconds{10};
inline constexpr u64 kHttpStallFloorBytesPerS = 1024;

// HttpClient refuses a zero `connect` and limits with neither `total` nor `stall`, so an override
// can never leave a call unbounded.
struct HttpLimits {
    std::chrono::milliseconds connect = kHttpConnectTimeout;
    std::optional<std::chrono::milliseconds> total;
    std::optional<ports::StallPolicy> stall;
};

[[nodiscard]] constexpr HttpLimits limits_for(HttpKind kind) noexcept {
    if (kind == HttpKind::Download)
        return {kHttpConnectTimeout, std::nullopt,
                ports::StallPolicy{kHttpStallFloorBytesPerS, default_deadline(OpKind::HttpDownload)}};
    return {kHttpConnectTimeout, default_deadline(OpKind::HttpSmall), std::nullopt};
}

// Applies to idempotent methods only; the delay doubles per attempt, with +-jitter, and a
// Retry-After up to `max_retry_after` replaces it.
struct RetryPolicy {
    u32 max_attempts = 3;
    std::chrono::milliseconds first_delay = std::chrono::seconds{1};
    u32 jitter_percent = 20;
    std::chrono::milliseconds max_retry_after = std::chrono::seconds{30};
};

inline constexpr RetryPolicy kNoRetry{.max_attempts = 1};

inline constexpr std::size_t kHttpDefaultMaxBody = 8u << 20;

// Public data only; credentials travel in HttpSecrets through HttpClient::send_secret.
struct HttpRequest {
    HttpMethod method = HttpMethod::Get;
    std::string url;
    std::vector<ports::HttpHeader> headers;
    std::vector<u8> body;
    HttpKind kind = HttpKind::Small;
    // Replaces limits_for(kind); a download may add a total here.
    std::optional<HttpLimits> limits;
    RetryPolicy retry;
    std::size_t max_body = kHttpDefaultMaxBody;
};

}  // namespace rb::net
