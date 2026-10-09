#include "reboot/testing/api_test_client.hpp"

#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "current_process.hpp"
#include "messages.hpp"
#include "reboot/api/v1/common.hpp"
#include "wire/codec.hpp"

namespace reboot::testing {

namespace ipc = contracts::ipc;

struct ApiTestClient::Impl {
    std::unique_ptr<ports::IByteStream> stream;
    FrameLog received{kIpcFrameCap};
    std::size_t handled = 0;
    u64 next_req = 1;
    u64 next_sub = 1;
    u64 next_nonce = 1;
    std::map<u64, u32> methods;
    std::optional<ipc::HelloAck> hello_ack;
    std::map<u64, ipc::Reply> replies;
    std::map<u64, u64> started;
    std::map<u64, api::Outcome> outcomes;
    std::map<u64, std::vector<api::Event>> events;
    std::map<u64, std::size_t> resyncs;
    std::vector<u32> foreground_hints;
    std::optional<ipc::GoodbyeReason> goodbye;
    bool closed = false;

    template <ContractMessage T>
    void send(const T& message) {
        if (closed || !stream) return;
        stream->write(encode_contract_frame(message));
    }

    void on_bytes(std::span<const u8> bytes) {
        (void)received.feed(bytes);
        const std::vector<OwnedFrame>& frames = received.frames();
        for (; handled < frames.size(); ++handled) handle(frames[handled]);
    }

    void handle(const OwnedFrame& frame) {
        const std::span<const u8> payload = frame.payload;
        if (frame.type == contract_frame_type_v<ipc::HelloAck>) {
            if (auto ack = decode_contract<ipc::HelloAck>(payload)) hello_ack = std::move(*ack);
        } else if (frame.type == contract_frame_type_v<ipc::Reply>) {
            if (auto reply = decode_contract<ipc::Reply>(payload)) replies.insert_or_assign(reply->req_id, std::move(*reply));
        } else if (frame.type == contract_frame_type_v<ipc::Started>) {
            if (auto start = decode_contract<ipc::Started>(payload)) started.insert_or_assign(start->req_id, start->op_id);
        } else if (frame.type == contract_frame_type_v<ipc::OpResult>) {
            if (auto result = decode_contract<ipc::OpResult>(payload))
                if (auto outcome = api::decode<api::Outcome>(result->outcome)) outcomes.insert_or_assign(result->op_id, std::move(*outcome));
        } else if (frame.type == contract_frame_type_v<ipc::EventBatch>) {
            auto batch = decode_contract<ipc::EventBatch>(payload);
            if (!batch) return;
            std::vector<api::Event>& into = events[batch->sub_id];
            // api::Event shares WireEvent's layout, so the bytes decode as one directly.
            for (const ipc::WireEvent& wire : batch->events)
                if (auto event = api::decode<api::Event>(sb::wire::encode_to_bytes(wire))) into.push_back(std::move(*event));
        } else if (frame.type == contract_frame_type_v<ipc::Resync>) {
            if (auto resync = decode_contract<ipc::Resync>(payload)) ++resyncs[resync->sub_id];
        } else if (frame.type == contract_frame_type_v<ipc::ForegroundHint>) {
            if (auto hint = decode_contract<ipc::ForegroundHint>(payload)) foreground_hints.push_back(hint->pid);
        } else if (frame.type == contract_frame_type_v<ipc::Goodbye>) {
            if (auto bye = decode_contract<ipc::Goodbye>(payload)) goodbye = bye->reason;
        }
    }
};

ApiTestClient::ApiTestClient(std::unique_ptr<ports::IByteStream> stream) : impl_(std::make_unique<Impl>()) {
    impl_->stream = std::move(stream);
    Impl* impl = impl_.get();
    impl_->stream->on_read([impl](std::span<const u8> bytes) { impl->on_bytes(bytes); });
    impl_->stream->on_close([impl] { impl->closed = true; });
}

ApiTestClient::~ApiTestClient() {
    // Its callbacks point at the Impl, so the stream goes first.
    impl_->stream.reset();
}

void ApiTestClient::hello(ipc::ClientKind kind, std::string client_build, ipc::CallerContext caller) {
    impl_->send(ipc::Hello{kind, std::move(client_build),
                           u32{VersionStreams::abi_major} << 16 | VersionStreams::abi_minor, current_process_id(),
                           std::move(caller)});
}

std::optional<ipc::HelloAck> ApiTestClient::hello_ack() const { return impl_->hello_ack; }

u64 ApiTestClient::call(u32 method_id, api::Bytes request, u32 timeout_ms) {
    const u64 req_id = impl_->next_req++;
    impl_->methods[req_id] = method_id;
    impl_->send(ipc::Call{req_id, method_id, std::move(request), timeout_ms});
    return req_id;
}

u64 ApiTestClient::start(u32 method_id, api::Bytes request, std::optional<bool> detached) {
    const u64 req_id = impl_->next_req++;
    impl_->methods[req_id] = method_id;
    impl_->send(ipc::Start{req_id, method_id, std::move(request), detached});
    return req_id;
}

std::optional<ipc::Reply> ApiTestClient::reply(u64 req_id) const {
    const auto it = impl_->replies.find(req_id);
    if (it == impl_->replies.end()) return std::nullopt;
    return it->second;
}

std::optional<Result<api::Bytes>> ApiTestClient::reply_payload(u64 req_id) const {
    const auto it = impl_->replies.find(req_id);
    if (it == impl_->replies.end()) return std::nullopt;
    const ipc::Reply& reply = it->second;
    if (reply.payload) return Result<api::Bytes>(*reply.payload);
    if (reply.error) return Result<api::Bytes>(std::unexpected(contracts::common::to_diagnostic(*reply.error)));
    return Result<api::Bytes>(make_diag(kTestingDomain, msg::kMalformedReply).arg("req_id", req_id).fail());
}

std::optional<u64> ApiTestClient::started_op(u64 req_id) const {
    const auto it = impl_->started.find(req_id);
    if (it == impl_->started.end()) return std::nullopt;
    return it->second;
}

std::optional<api::Outcome> ApiTestClient::outcome(u64 op_id) const {
    const auto it = impl_->outcomes.find(op_id);
    if (it == impl_->outcomes.end()) return std::nullopt;
    return it->second;
}

void ApiTestClient::cancel(u64 op_id) { impl_->send(ipc::Cancel{op_id}); }

void ApiTestClient::attach(u64 op_id) { impl_->send(ipc::Attach{op_id}); }

void ApiTestClient::release(u64 op_id) { impl_->send(ipc::Release{op_id}); }

u64 ApiTestClient::subscribe(const api::EventFilter& filter) {
    const u64 sub_id = impl_->next_sub++;
    impl_->events.try_emplace(sub_id);
    impl_->send(ipc::Subscribe{sub_id, api::encode(filter)});
    return sub_id;
}

void ApiTestClient::unsubscribe(u64 sub_id) { impl_->send(ipc::Unsubscribe{sub_id}); }

void ApiTestClient::credit(u64 sub_id, u32 n) { impl_->send(ipc::Credit{sub_id, n}); }

std::vector<api::Event> ApiTestClient::events(u64 sub_id) const {
    const auto it = impl_->events.find(sub_id);
    if (it == impl_->events.end()) return {};
    return it->second;
}

std::size_t ApiTestClient::resyncs(u64 sub_id) const {
    const auto it = impl_->resyncs.find(sub_id);
    return it == impl_->resyncs.end() ? 0 : it->second;
}

void ApiTestClient::secret_put(std::span<const u8> target, std::span<const u8> secret) {
    impl_->send(ipc::SecretPut{{target.begin(), target.end()}, {secret.begin(), secret.end()}});
}

u64 ApiTestClient::secret_reveal(std::span<const u8> target) {
    const u64 req_id = impl_->next_req++;
    impl_->methods[req_id] = 0;
    impl_->send(ipc::SecretReveal{req_id, {target.begin(), target.end()}});
    return req_id;
}

void ApiTestClient::log_write(LogLevel level, std::string text) { impl_->send(ipc::LogWrite{level, std::move(text)}); }

void ApiTestClient::ping() { impl_->send(contracts::common::Ping{impl_->next_nonce++}); }

void ApiTestClient::goodbye() { impl_->send(ipc::Goodbye{ipc::GoodbyeReason::Normal}); }

void ApiTestClient::close() {
    if (impl_->stream) impl_->stream->close();
}

std::vector<u32> ApiTestClient::foreground_hints() const { return impl_->foreground_hints; }

std::optional<ipc::GoodbyeReason> ApiTestClient::goodbye_received() const { return impl_->goodbye; }

bool ApiTestClient::closed() const { return impl_->closed; }

const FrameLog& ApiTestClient::received() const noexcept { return impl_->received; }

u32 ApiTestClient::method_of(u64 req_id) const {
    const auto it = impl_->methods.find(req_id);
    return it == impl_->methods.end() ? 0 : it->second;
}

}  // namespace reboot::testing
