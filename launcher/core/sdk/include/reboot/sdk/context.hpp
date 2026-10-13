#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "reboot/api/codec.hpp"
#include "reboot/api/v1/common.hpp"
#include "reboot/api/v1/engine.hpp"
#include "reboot/api/v1/method_table.hpp"
#include "reboot/api/v1/secrets.hpp"
#include "reboot/base/secret.hpp"
#include "reboot/client.h"
#include "reboot/sdk/buffer.hpp"
#include "reboot/sdk/connection.hpp"
#include "reboot/sdk/error.hpp"
#include "reboot/sdk/methods.hpp"
#include "reboot/sdk/op.hpp"
#include "reboot/sdk/subscription.hpp"

namespace rb::sdk {

enum class ClientKind : std::uint32_t {
    Unknown = RB_CLIENT_UNKNOWN,
    WindowsGui = RB_CLIENT_WINDOWS_GUI,
    MacGui = RB_CLIENT_MAC_GUI,
    LinuxGui = RB_CLIENT_LINUX_GUI,
    Cli = RB_CLIENT_CLI,
    Test = RB_CLIENT_TEST,
};

enum class LaunchMode : std::uint32_t {
    // Starts the engine when none answers, and reconnects after a loss.
    Autostart = RB_LAUNCH_AUTOSTART,
    // Never starts an engine and never reconnects.
    ConnectOnly = RB_LAUNCH_CONNECT_ONLY,
};

enum class LogLevel : std::int32_t {
    Trace = RB_LOG_TRACE,
    Debug = RB_LOG_DEBUG,
    Info = RB_LOG_INFO,
    Warn = RB_LOG_WARN,
    Error = RB_LOG_ERROR,
};

struct ConnectOptions {
    // UTF-8 and absolute; nullopt means REBOOT_LAUNCHER_HOME, then the platform default.
    std::optional<std::string> data_root;
    ClientKind client_kind = ClientKind::Unknown;
    LaunchMode launch_mode = LaunchMode::Autostart;
    // Zero means reboot_client's default.
    std::chrono::milliseconds connect_deadline{0};
    // What this program was built against: reboot_client must have the same ABI major, and the
    // engine the same schema.
    std::uint32_t abi_version = (std::uint32_t{RB_ABI_MAJOR} << 16) | RB_ABI_MINOR;
    std::string_view schema_fingerprint = RB_SCHEMA_FINGERPRINT;
    // Keeps a connection to an engine of another build, which serves only the bootstrap subset:
    // Engine.status, drain, shutdown and restart_when_idle, Sessions.list and stop.
    bool allow_bootstrap_only = false;
};

// One connection to the engine, shared by its copies; every reboot.api.v1 method is a typed member
// (methods.hpp). Ops and subscriptions keep the connection alive. Any member may be called from any
// thread.
class Context : public Methods<Context> {
public:
    [[nodiscard]] static Expected<Context> connect(const ConnectOptions& options = {}) {
        const std::uint32_t library = rb_abi_version();
        if ((library >> 16) != (options.abi_version >> 16))
            return std::unexpected(Error::local(RB_E_ABI_MISMATCH, "sdk.abi_mismatch",
                                                {{"expected", api::ArgKind::Unsigned, std::to_string(options.abi_version >> 16)},
                                                 {"library", api::ArgKind::Unsigned, std::to_string(library >> 16)}},
                                                api::ErrorKind::Unsupported));
        rb_ctx_options raw{};
        raw.struct_size = sizeof raw;
        raw.data_root = options.data_root ? options.data_root->c_str() : nullptr;
        raw.client_kind = static_cast<std::uint32_t>(options.client_kind);
        raw.launch_mode = static_cast<std::uint32_t>(options.launch_mode);
        raw.connect_deadline_ms = static_cast<std::uint32_t>(options.connect_deadline.count());
        rb_ctx* handle = nullptr;
        const rb_status status = rb_ctx_create(&raw, &handle);
        if (status != RB_OK) return std::unexpected(Error::last(status));
        Context context(std::make_shared<detail::ContextState>(handle));
        Buffer warning;
        if (rb_last_error(warning.out()) == RB_OK && !warning.empty()) context.warning_ = Error::decode(RB_OK, warning.bytes());

        Expected<api::EngineInfoResponse> info = context.engine_info();
        if (!info && info.error().status() == RB_E_ENGINE_VERSION_MISMATCH && options.allow_bootstrap_only) return context;
        if (!info) return std::unexpected(std::move(info.error()));
        if (info->schema_fingerprint != options.schema_fingerprint)
            return std::unexpected(Error::local(RB_E_ENGINE_VERSION_MISMATCH, "sdk.schema_mismatch",
                                                {{"client", api::ArgKind::String, std::string(options.schema_fingerprint)},
                                                 {"engine", api::ArgKind::String, info->schema_fingerprint}},
                                                api::ErrorKind::Unsupported));
        context.engine_ = std::move(*info);
        return context;
    }

    // A warning from connecting, such as ipc.engine_image_differs.
    [[nodiscard]] const std::optional<Error>& warning() const noexcept { return warning_; }
    // Engine.info at connect; nullopt for an engine of another build, which serves only the bootstrap subset.
    [[nodiscard]] const std::optional<api::EngineInfoResponse>& engine() const noexcept { return engine_; }
    [[nodiscard]] bool bootstrap_only() const noexcept { return !engine_.has_value(); }
    [[nodiscard]] ConnectionState connection() const noexcept { return state_->connection.load(); }
    [[nodiscard]] rb_ctx* native() const noexcept { return state_->ctx; }
    [[nodiscard]] const detail::SharedContext& shared() const noexcept { return state_; }

    // A CALL method by id; zero `timeout` lets the engine apply the method's default.
    template <class Response, class Request>
    [[nodiscard]] Expected<Response> call(std::uint32_t method, const Request& request,
                                          std::chrono::milliseconds timeout = {}) const {
        const api::Bytes encoded = api::encode(request);
        Buffer response;
        const rb_status status = rb_call(state_->ctx, method, encoded.data(), encoded.size(),
                                         static_cast<std::uint32_t>(timeout.count()), response.out());
        if (status == RB_E_REMOTE) return std::unexpected(Error::decode(status, response.bytes()));
        if (status != RB_OK) return std::unexpected(Error::last(status));
        std::expected<Response, api::DecodeError> decoded = api::decode<Response>(response.bytes());
        if (!decoded)
            return std::unexpected(Error::local(RB_E_REMOTE, "sdk.malformed_reply", {{"what", api::ArgKind::String, "response"}}));
        return std::move(*decoded);
    }

    // An OPERATION method by id.
    template <class Response, class Request>
    [[nodiscard]] Expected<Op<Response>> start(std::uint32_t method, const Request& request,
                                               Disconnect disconnect = Disconnect::Default) const {
        const api::Bytes encoded = api::encode(request);
        const std::uint32_t flags = disconnect == Disconnect::Detached ? RB_START_DETACHED
                                    : disconnect == Disconnect::Bound  ? RB_START_BOUND
                                                                       : 0u;
        std::uint64_t op_id = 0;
        const rb_status status = rb_start(state_->ctx, method, encoded.data(), encoded.size(), flags, &op_id);
        if (status != RB_OK) return std::unexpected(Error::last(status));
        return Op<Response>(state_, op_id, method);
    }

    // An operation started elsewhere: by another client, or before a reconnect.
    template <class Response>
    [[nodiscard]] Expected<Op<Response>> attach(std::uint32_t method, std::uint64_t op_id) const {
        const rb_status status = rb_op_attach(state_->ctx, op_id);
        if (status != RB_OK) return std::unexpected(Error::last(status));
        return Op<Response>(state_, op_id, method);
    }

    [[nodiscard]] Expected<Subscription> subscribe(const api::EventFilter& filter = {}) const {
        return Subscription::open(state_, filter);
    }

    // Stores a secret; reboot_client wipes its copy once sent, and `secret` wipes itself.
    Expected<void> put_secret(const api::SecretTarget& target, const SecretString& secret) const {
        const api::Bytes encoded = api::encode(target);
        const std::string& bytes = secret.reveal();
        const rb_status status = rb_secret_put(state_->ctx, encoded.data(), encoded.size(),
                                               reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        if (status != RB_OK) return std::unexpected(Error::last(status));
        return {};
    }

    // Reads back a host join password, the only revealable secret.
    [[nodiscard]] Expected<SecretString> reveal_secret(const api::SecretTarget& target) const {
        const api::Bytes encoded = api::encode(target);
        Buffer out;
        const rb_status status = rb_secret_reveal(state_->ctx, encoded.data(), encoded.size(), out.out());
        if (status != RB_OK) return std::unexpected(Error::last(status));
        const std::span<const std::uint8_t> bytes = out.bytes();
        return SecretString(std::string(bytes.begin(), bytes.end()));
    }

    // A line in the engine log under the client category; secrets are redacted there.
    void log(LogLevel level, std::string_view utf8) const {
        rb_log_write(state_->ctx, static_cast<std::int32_t>(level), utf8.data(), utf8.size());
    }

private:
    explicit Context(detail::SharedContext state) : state_(std::move(state)) {}

    detail::SharedContext state_;
    std::optional<Error> warning_;
    std::optional<api::EngineInfoResponse> engine_;
};

}  // namespace rb::sdk
