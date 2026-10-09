#pragma once

#include <memory>

#include "reboot/foundation/executor.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::testing {

// The connection both ends of a memory stream pair share.
struct MemoryLink;

struct LinkedPair {
    std::unique_ptr<ports::IByteStream> a;
    std::unique_ptr<ports::IByteStream> b;
    std::shared_ptr<MemoryLink> link;
};

[[nodiscard]] LinkedPair make_linked_pair(Executor& deliver_on, ports::PeerIdentity a_peer, ports::PeerIdentity b_peer);

// As if either end called close().
void close_link(const std::shared_ptr<MemoryLink>& link);
[[nodiscard]] bool link_open(const MemoryLink& link);

}  // namespace rb::testing
