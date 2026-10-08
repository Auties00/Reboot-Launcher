#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::publish {

// IdentityRotated: UNAUTHORIZED replaced the server id, so the share link changed.
// HostedElsewhere: CONFLICT, the id is now published from another connection.
// TokenNotSaved: a new token is only in memory; losing it locks the id for 30 days.
enum class PublishNoticeKind : u8 { IdentityRotated, HostedElsewhere, TokenNotSaved };

// Must reach the user, so it goes to IPublishNoticeSink instead of a coalesced event.
// `message` is a Warning with the ids and server ids as args.
struct PublishNotice {
    PublishNoticeKind kind{};
    SessionId session;
    HostProfileId profile;
    Diagnostic message;
};

}  // namespace reboot::publish
