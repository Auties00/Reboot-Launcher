#pragma once

#include "reboot/browser/server_row.hpp"

namespace rb::browser {

// Payload of UserRequestKind::ConfirmJoin. The UI shows the hidden, offline and password flags
// from `server.row` before the user agrees.
struct ConfirmJoinPrompt {
    ServerDetails server;
};

// The only accepted answer to ConfirmJoin.
struct ConfirmJoinAnswer {
    bool accept = false;
};

// Payload of UserRequestKind::NeedsJoinPassword. The client puts the password as the JoinPassword
// secret scoped to this request, then answers JoinPasswordProvided; cancelling refuses the join.
struct NeedsJoinPasswordPrompt {
    ServerRow server;
    // The edge answered WRONG_PASSWORD to the previous one.
    bool retry = false;
};

struct JoinPasswordProvided {};

}  // namespace rb::browser
