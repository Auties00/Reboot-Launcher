#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "reboot/api/codec.hpp"
#include "reboot/api/decode_error.hpp"
#include "reboot/api/v1/common.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/testing/frame_log.hpp"

namespace reboot::testing {

// Covers no capability ids (decisions testing-strategy, async-event-model).
// What reboot_client does over an IByteStream, minus the C ABI, typed with reboot.api.v1, keeping
// every engine frame. Strand-only: the stream's callbacks are expected on the test's ManualExecutor.
class ApiTestClient {
public:
    explicit ApiTestClient(std::unique_ptr<ports::IByteStream> stream);
    ~ApiTestClient();
    ApiTestClient(const ApiTestClient&) = delete;
    ApiTestClient& operator=(const ApiTestClient&) = delete;

    // Engine IPC is private (owner decision 6), so another `client_build` gets BootstrapOnly.
    void hello(contracts::ipc::ClientKind kind = contracts::ipc::ClientKind::Test,
               std::string client_build = std::string(VersionStreams::ipc_build),
               contracts::ipc::CallerContext caller = {});
    [[nodiscard]] std::optional<contracts::ipc::HelloAck> hello_ack() const;

    // Each returns its req_id; answers are looked up later, after the runtime ran.
    u64 call(u32 method_id, api::Bytes request, u32 timeout_ms = 0);
    template <class Request>
    u64 call(u32 method_id, const Request& request) {
        return call(method_id, api::encode(request));
    }
    // No `detached` means the method's default disconnect policy.
    u64 start(u32 method_id, api::Bytes request, std::optional<bool> detached = std::nullopt);
    template <class Request>
    u64 start(u32 method_id, const Request& request, std::optional<bool> detached = std::nullopt) {
        return start(method_id, api::encode(request), detached);
    }

    [[nodiscard]] std::optional<contracts::ipc::Reply> reply(u64 req_id) const;
    // nullopt until the Reply arrived; then the payload, the engine's Diagnostic, or
    // testing.malformed_reply for a Reply holding neither.
    [[nodiscard]] std::optional<Result<api::Bytes>> reply_payload(u64 req_id) const;
    // As reply_payload, decoded; an undecodable payload fails as api::to_diagnostic reports it.
    template <class Response>
    [[nodiscard]] std::optional<Result<Response>> response(u64 req_id) const {
        std::optional<Result<api::Bytes>> payload = reply_payload(req_id);
        if (!payload) return std::nullopt;
        if (!*payload) return Result<Response>(std::unexpected(std::move(payload->error())));
        auto decoded = api::decode<Response>(**payload);
        if (!decoded) return Result<Response>(std::unexpected(api::to_diagnostic(decoded.error(), method_of(req_id))));
        return Result<Response>(std::move(*decoded));
    }
    [[nodiscard]] std::optional<u64> started_op(u64 req_id) const;
    [[nodiscard]] std::optional<api::Outcome> outcome(u64 op_id) const;

    void cancel(u64 op_id);
    void attach(u64 op_id);
    void release(u64 op_id);

    // Returns the sub_id; events and Resyncs collect per subscription.
    u64 subscribe(const api::EventFilter& filter);
    void unsubscribe(u64 sub_id);
    void credit(u64 sub_id, u32 n);
    [[nodiscard]] std::vector<api::Event> events(u64 sub_id) const;
    [[nodiscard]] std::size_t resyncs(u64 sub_id) const;

    void secret_put(std::span<const u8> target, std::span<const u8> secret);
    u64 secret_reveal(std::span<const u8> target);
    void log_write(LogLevel level, std::string text);
    void ping();
    void goodbye();
    void close();

    [[nodiscard]] std::vector<u32> foreground_hints() const;
    [[nodiscard]] std::optional<contracts::ipc::GoodbyeReason> goodbye_received() const;
    [[nodiscard]] bool closed() const;
    [[nodiscard]] const FrameLog& received() const noexcept;

private:
    // The method a req_id called.
    [[nodiscard]] u32 method_of(u64 req_id) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::testing
