#include "redirect_guard.hpp"

#include <algorithm>
#include <utility>

#include "url.hpp"

namespace rb::net {

namespace {

// Scheme and authority without user info: a server's Location may carry credentials or a signed query.
[[nodiscard]] std::string redacted(std::string_view url) {
    const std::size_t separator = url.find("://");
    const std::size_t start = separator == std::string_view::npos ? 0 : separator + 3;
    std::string_view authority = url.substr(start);
    authority = authority.substr(0, authority.find_first_of("/?#"));
    if (const std::size_t at = authority.rfind('@'); at != std::string_view::npos) authority.remove_prefix(at + 1);
    return std::string(url.substr(0, start)) + std::string(authority);
}

}  // namespace

RedirectGuard::RedirectGuard(std::vector<std::string> plain_http_hosts, std::vector<std::string> https_hosts) {
    for (const std::string& host : plain_http_hosts) plain_http_hosts_.push_back(normalize_host(host));
    for (const std::string& host : https_hosts) https_seen_.push_back(normalize_host(host));
}

std::optional<HttpError> RedirectGuard::admit(std::string_view url) {
    const std::optional<ParsedUrl> parsed = parse_url(url);
    if (!parsed) return HttpError{.code = HttpErrorCode::InvalidUrl, .host = redacted(url)};
    if (is_literal_loopback(parsed->host)) return std::nullopt;
    if (parsed->scheme == UrlScheme::Https) {
        if (std::ranges::find(https_seen_, parsed->host) == https_seen_.end()) https_seen_.push_back(parsed->host);
        return std::nullopt;
    }
    if (std::ranges::find(https_seen_, parsed->host) != https_seen_.end())
        return HttpError{.code = HttpErrorCode::DowngradeRefused, .host = parsed->host};
    if (std::ranges::find(plain_http_hosts_, parsed->host) == plain_http_hosts_.end())
        return HttpError{.code = HttpErrorCode::PlainHttpNeedsConsent, .host = parsed->host};
    return std::nullopt;
}

}  // namespace rb::net
