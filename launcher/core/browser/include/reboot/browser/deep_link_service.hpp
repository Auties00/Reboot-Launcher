#pragma once

#include <memory>
#include <string_view>

#include "reboot/browser/server_row.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot {
class UserRequestRegistry;
}

namespace reboot::browser {

class BrowserSession;
class GameServerTarget;
class IOwnServers;

// The op's value once the user confirmed: the server is now the join target.
struct LinkResolution {
    ServerDetails server;
};

// Capabilities: server-browser.deep-link-join, server-browser.+21, server-browser.+34, server-browser.+38, server-browser.+67, server-browser.+88.
// Strand-only. Resolve by id, then ConfirmJoin; only an accepted answer sets the GameServerTarget.
// A link never joins or launches by itself, and a newer link supersedes a pending one.
class DeepLinkService {
public:
    // OpKind::Generic, 120 s, which the ConfirmJoin wait suspends (OperationBase::awaiting_user).
    static constexpr OpKind kOpKind = OpKind::Generic;

    DeepLinkService(BrowserSession& session, GameServerTarget& target, const IOwnServers& own,
                    UserRequestRegistry& requests, OpRegistry& ops);
    ~DeepLinkService();
    DeepLinkService(const DeepLinkService&) = delete;
    DeepLinkService& operator=(const DeepLinkService&) = delete;

    // An Operation<LinkResolution>. Fails synchronously with parse_deep_link's error or
    // browser.join_own_server. An id the edge does not know fails with browser.link_not_found.
    [[nodiscard]] Result<OpHandle> start_resolve(std::string_view link, DisconnectPolicy policy);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::browser
