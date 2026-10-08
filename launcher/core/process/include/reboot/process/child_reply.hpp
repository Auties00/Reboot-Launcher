#pragma once

#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::process {

class ChildSupervisor;

// A private message that carries a req_id (field 1, as every contract request and reply does).
template <class T>
concept CorrelatedMessage = ContractMessage<T> && requires(T& message) { message.req_id = u64{}; };

// Covers no capability ids. The one answer to one child-to-engine request. Strand-only and
// move-only. A reply after the child was respawned is dropped, and a request dropped unanswered
// gets CommandResult{ok = false, internal.bug}. The supervisor detaches every outstanding reply
// when it is destroyed, so a handler may hold one past it; answering it then does nothing.
class ChildReply {
public:
    ChildReply(ChildReply&& other) noexcept;
    ChildReply& operator=(ChildReply&& other) noexcept;
    ChildReply(const ChildReply&) = delete;
    ChildReply& operator=(const ChildReply&) = delete;
    ~ChildReply();

    [[nodiscard]] u64 req_id() const noexcept { return req_id_; }

    // The typed reply (backend MatchTarget); its req_id is filled in here.
    template <CorrelatedMessage T>
    void reply(T message) {
        message.req_id = req_id_;
        send(encode_contract_frame(message));
    }
    void ok();
    void fail(const Diagnostic& error);
    void unsupported();

private:
    friend class ChildSupervisor;
    ChildReply(ChildSupervisor& supervisor, u64 req_id, u32 generation) noexcept;

    void send(std::vector<u8> frame);
    // Answers an unanswered reply with the internal.bug CommandResult.
    void drop();

    // Null once answered, moved from, or detached by the supervisor's destructor.
    ChildSupervisor* supervisor_ = nullptr;
    u64 req_id_ = 0;
    u32 generation_ = 0;
};

}  // namespace reboot::process
