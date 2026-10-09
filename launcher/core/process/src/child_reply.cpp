#include "reboot/process/child_reply.hpp"

#include <utility>

#include "reboot/contracts/common.hpp"
#include "reboot/process/child_supervisor.hpp"

namespace rb::process {

ChildReply::ChildReply(ChildSupervisor& supervisor, u64 req_id, u32 generation) noexcept
    : supervisor_(&supervisor), req_id_(req_id), generation_(generation) {
    supervisor_->track(*this);
}

ChildReply::ChildReply(ChildReply&& other) noexcept
    : supervisor_(std::exchange(other.supervisor_, nullptr)), req_id_(other.req_id_), generation_(other.generation_) {
    if (supervisor_ != nullptr) supervisor_->retrack(other, *this);
}

ChildReply& ChildReply::operator=(ChildReply&& other) noexcept {
    if (this != &other) {
        drop();
        supervisor_ = std::exchange(other.supervisor_, nullptr);
        req_id_ = other.req_id_;
        generation_ = other.generation_;
        if (supervisor_ != nullptr) supervisor_->retrack(other, *this);
    }
    return *this;
}

ChildReply::~ChildReply() { drop(); }

void ChildReply::drop() {
    if (supervisor_ == nullptr) return;
    // Unanswered: the child still gets its one answer.
    contracts::common::CommandResult result;
    result.req_id = req_id_;
    result.error = contracts::common::to_wire(internal_bug("process.child_reply_dropped"));
    send(encode_contract_frame(result));
}

void ChildReply::ok() {
    contracts::common::CommandResult result;
    result.req_id = req_id_;
    result.ok = true;
    send(encode_contract_frame(result));
}

void ChildReply::fail(const Diagnostic& error) {
    contracts::common::CommandResult result;
    result.req_id = req_id_;
    result.error = contracts::common::to_wire(error);
    send(encode_contract_frame(result));
}

void ChildReply::unsupported() { send(encode_contract_frame(contracts::common::Unsupported{req_id_})); }

void ChildReply::send(std::vector<u8> frame) {
    if (supervisor_ == nullptr) return;
    ChildSupervisor* supervisor = std::exchange(supervisor_, nullptr);
    supervisor->untrack(*this);
    supervisor->send_reply(generation_, std::move(frame));
}

}  // namespace rb::process
