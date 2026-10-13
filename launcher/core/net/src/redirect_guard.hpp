#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/net/http_error.hpp"

namespace rb::net {

// HostTlsMemory's rules for each hop of one plain http transfer, on the transport's thread: a
// plain hop reaches only a host HttpClient allowed, and never a host seen on https before or by
// this chain. Literal loopback hosts are exempt, as in HostTlsMemory.
class RedirectGuard {
public:
    explicit RedirectGuard(std::vector<std::string> plain_http_hosts, std::vector<std::string> https_hosts = {});

    // nullopt to go on; otherwise the error that ends the transfer.
    [[nodiscard]] std::optional<HttpError> admit(std::string_view url);

private:
    std::vector<std::string> plain_http_hosts_;
    std::vector<std::string> https_seen_;
};

}  // namespace rb::net
