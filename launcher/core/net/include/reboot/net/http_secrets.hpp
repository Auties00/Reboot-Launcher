#pragma once

#include <string>
#include <vector>

#include "reboot/foundation/secret.hpp"

namespace reboot::net {

struct SecretHeader {
    std::string name;
    SecretString value;
};

// The credentials of one request, such as a password grant body or an Authorization value.
// Wiped on destruction and never logged; kept apart from HttpRequest so that stays copyable.
struct HttpSecrets {
    // Sent after HttpRequest::headers.
    std::vector<SecretHeader> headers;
    // Replaces HttpRequest::body when not empty.
    SecretBytes body;
};

}  // namespace reboot::net
