#pragma once

#include "reboot/foundation/types.hpp"

namespace reboot::front {

// ThirdPartyAuthDll: a LegacyFixed session under Wine, where 127.0.0.1:80 is not bound, so the custom
// DLL's XMPP has no server.
enum class XmppUnavailableReason : u8 { ThirdPartyAuthDll };

// Payload of EventKind::XmppUnavailable, published with the session's scope.
struct XmppUnavailable {
    SessionId session;
    XmppUnavailableReason reason{};
};

}  // namespace reboot::front
