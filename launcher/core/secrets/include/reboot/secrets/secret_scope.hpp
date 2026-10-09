#pragma once

#include <compare>
#include <string>
#include <utility>

#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::secrets {

// Which secret of a kind; held as canonical text so it doubles as the wire and store form.
class SecretScope {
public:
    // The host is lowercased; the port is kept only when given.
    [[nodiscard]] static SecretScope backend(const HostPort& backend);
    [[nodiscard]] static SecretScope host_profile(HostProfileId profile);
    [[nodiscard]] static SecretScope join_request(RequestId request);

    [[nodiscard]] const std::string& text() const noexcept { return text_; }

    auto operator<=>(const SecretScope&) const = default;

private:
    explicit SecretScope(std::string text) : text_(std::move(text)) {}

    std::string text_;
};

}  // namespace rb::secrets
