#pragma once

#include <string>

namespace rb::backend {

// Payload of the ConfirmUnencryptedUpstream request BackendService raises for a plain-http upstream.
struct UnencryptedUpstreamPrompt {
    // As BackendUrl::origin() writes it.
    std::string origin;
};

// The only accepted answer; an accepted host is remembered in HostTlsMemory.
struct UnencryptedUpstreamAnswer {
    bool accept = false;
};

}  // namespace rb::backend
