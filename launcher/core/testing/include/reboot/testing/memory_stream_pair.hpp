#pragma once

#include <memory>

#include "reboot/foundation/executor.hpp"
#include "reboot/ports/ipc.hpp"

namespace reboot::testing {

struct MemoryStreamPair {
    std::unique_ptr<ports::IByteStream> a;
    std::unique_ptr<ports::IByteStream> b;
};

// Covers no capability ids (decision testing-strategy).
// Two connected IByteStream ends delivering in write order through `deliver_on`; close() or
// destruction of either reaches both once. `a_peer` is what a.peer() reports: whoever holds b.
[[nodiscard]] MemoryStreamPair make_memory_stream_pair(Executor& deliver_on, ports::PeerIdentity a_peer,
                                                       ports::PeerIdentity b_peer);

}  // namespace reboot::testing
