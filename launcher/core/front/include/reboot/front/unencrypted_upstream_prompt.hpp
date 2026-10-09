#pragma once

#include <string>

namespace rb::front {

// ConfirmUnencryptedUpstream payload the front raises for a learned plain-ws origin; the relay gets 502 until accepted.
struct UnencryptedUpstreamPrompt {
    // As UpstreamOrigin::to_string() writes it.
    std::string origin;
};

// The answer the front accepts; without `remember` the consent lasts as long as the route.
struct UnencryptedUpstreamAnswer {
    bool accept = false;
    bool remember = false;
};

}  // namespace rb::front
