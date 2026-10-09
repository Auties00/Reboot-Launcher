#pragma once

#include <memory>
#include <optional>

#include "reboot/browser/join_outcome.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace rb {
class IClock;
class UserRequestRegistry;
}  // namespace rb

namespace rb::browser {

class BrowserSession;
class IOwnServers;

// Takes the JoinPassword secret a client put for a NeedsJoinPassword request; the engine wires it
// to the secret service. Empty when nothing was put.
using JoinPasswordSource = UniqueFunction<std::optional<SecretString>(RequestId request)>;

// AlreadyConfirmed: the target came from a ConfirmJoin the user already answered.
enum class JoinConfirmation : u8 { Ask, AlreadyConfirmed };

struct JoinRequest {
    ServerId server;
    JoinConfirmation confirmation = JoinConfirmation::Ask;
    // The build Play will launch. Unless same_game_version matches the resolved server, the join
    // fails with VersionMismatch before ConfirmJoin and before a Join spends a token. Absent only
    // on the address-only path, which launches nothing.
    std::optional<GameVersion> local_version;
};

// Capabilities: server-browser.join, server-browser.+21, server-browser.+31.
// Strand-only. Resolve, version check, ConfirmJoin, then Join for a fresh JoinGrant; own servers
// are refused before anything is sent. Joins are paced per server, as the edge limits them.
class JoinService {
public:
    // OpKind::Generic, 120 s, which the ConfirmJoin and password waits suspend.
    static constexpr OpKind kOpKind = OpKind::Generic;

    JoinService(BrowserSession& session, const IOwnServers& own, UserRequestRegistry& requests, OpRegistry& ops,
                const IClock& clock, JoinPasswordSource passwords);
    ~JoinService();
    JoinService(const JoinService&) = delete;
    JoinService& operator=(const JoinService&) = delete;

    // Runs inside the caller's op (Play.start): its token cancels the join and its prompts suspend
    // its deadline. Fails synchronously with browser.join_own_server. `done` runs on the strand
    // exactly once, with a JoinFailure turned into its Diagnostic.
    [[nodiscard]] Result<void> join(JoinRequest request, OperationBase& op, std::optional<SessionId> session,
                                    UniqueFunction<void(Result<JoinOutcome>)> done);

    // An Operation<JoinOutcome> that only returns the granted address, for "Copy IP" when the
    // backend is not embedded. Fails synchronously like join().
    [[nodiscard]] Result<OpHandle> start_join(JoinRequest request, DisconnectPolicy policy);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::browser
