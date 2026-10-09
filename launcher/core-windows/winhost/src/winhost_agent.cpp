#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/winhost/winhost_agent.hpp"

#include <algorithm>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <variant>

#include "env_overlay.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/os_windows/win32session/win32_session.hpp"
#include "reboot/os_windows/winhost/control_connection.hpp"

namespace reboot::os_windows::winhost {
namespace {

namespace wh = contracts::winhost;
namespace common = contracts::common;
using win32session::SpawnError;
using win32session::SpawnStep;

void wipe(wh::Bytes& bytes) noexcept { secure_wipe(bytes.data(), bytes.size()); }

// The argv and environment carry -AUTH_PASSWORD and the game's REBOOT_CTL_TOKEN.
void wipe_secrets(wh::SpawnGame& spawn) noexcept {
    for (auto& arg : spawn.argv_utf16) wipe(arg);
    wipe(spawn.env_block_utf16);
}

// A SpawnGame whose secrets are wiped however it ends, also when it is dropped unlaunched.
struct LaunchRequest {
    wh::SpawnGame spawn;

    explicit LaunchRequest(wh::SpawnGame value) noexcept : spawn(std::move(value)) {}
    LaunchRequest(LaunchRequest&&) noexcept = default;
    LaunchRequest& operator=(LaunchRequest&&) = delete;
    ~LaunchRequest() { wipe_secrets(spawn); }
};

using Request = std::variant<LaunchRequest, wh::Resume, wh::Inject, wh::Stop>;

// Field 1 of any request, read without knowing its type.
struct RequestId {
    u64 req_id = 0;
};

template <ContractMessage T>
std::optional<T> decode_frame(const ControlFrame& frame) {
    T message{};
    if (sb::wire::decode(frame.payload.reveal(), message)) return message;
    // A Welcome cut short may still hold part of the argv and environment.
    if constexpr (std::same_as<T, wh::WhWelcome>) wipe_secrets(message.spawn);
    return std::nullopt;
}

WinhostFailure from_spawn(const SpawnError& error, FailureStep otherwise) {
    return WinhostFailure{error.step == SpawnStep::Inject ? FailureStep::Inject : otherwise, error.error.code};
}

}  // namespace

struct WinhostAgent::Impl {
    explicit Impl(ControlConnection& conn) : connection(conn) {}

    ControlConnection& connection;

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Request> queue;
    bool closing = false;
    // Set by the request thread when it ends the channel itself.
    std::optional<WinhostExit> outcome;
    std::thread worker;

    // Request thread only.
    std::unique_ptr<win32session::Win32Session> session;

    template <ContractMessage T>
    void send(const T& message) {
        static_cast<void>(connection.send(message));
    }

    // Win32Session calls it from its pipe-reader and exit-wait threads too.
    void relay(win32session::SessionEvent event) {
        try {
            if (auto* output = std::get_if<wh::Output>(&event); output && output->bytes.size() > kOutputChunkBytes) {
                const std::span<const u8> bytes{output->bytes};
                for (std::size_t at = 0; at < bytes.size(); at += kOutputChunkBytes) {
                    const auto chunk = bytes.subspan(at, std::min(kOutputChunkBytes, bytes.size() - at));
                    send(wh::Output{output->role, output->stream, wh::Bytes(chunk.begin(), chunk.end())});
                }
                return;
            }
            std::visit([this](const auto& message) { send(message); }, event);
        } catch (...) {
            // A lost event is not worth ending a watcher; the engine notices a dead channel itself.
        }
    }

    void enqueue(Request request) {
        {
            const std::scoped_lock lock(mutex);
            if (closing) return;
            queue.push_back(std::move(request));
        }
        wake.notify_one();
    }

    // Ends the channel from the request thread: run() then sees EOF and returns `exit`.
    void finish(WinhostExit exit) {
        {
            const std::scoped_lock lock(mutex);
            if (!outcome) outcome = exit;
            closing = true;
            queue.clear();
        }
        connection.shutdown();
    }

    void serve() {
        try {
            for (;;) {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return closing || !queue.empty(); });
                if (closing) break;
                Request request = std::move(queue.front());
                queue.pop_front();
                lock.unlock();
                std::visit([this](auto& typed) { handle(typed); }, request);
            }
            // EOF, an error or a Stop: the Job goes now and its last events are sent first.
            session.reset();
        } catch (...) {
            session.reset();
            finish(WinhostExit::InternalError);
        }
    }

    void handle(LaunchRequest& request) {
        wh::SpawnGame& spawn = request.spawn;
        // The engine's block holds only the game's channel layer; the prefix's own variables come
        // from winhost's environment, from which read_bootstrap() already removed REBOOT_CTL*.
        wh::Bytes layered = overlay_own_environment(spawn.env_block_utf16);
        wipe(spawn.env_block_utf16);
        spawn.env_block_utf16 = std::move(layered);
        auto launched =
            win32session::launch_session(spawn, [this](win32session::SessionEvent event) { relay(std::move(event)); });
        wipe_secrets(spawn);
        if (!launched) {
            send(to_fatal(from_spawn(launched.error(), FailureStep::Spawn)));
            finish(WinhostExit::LaunchFailed);
            return;
        }
        session = std::move(*launched);
    }

    void handle(const wh::Resume& request) {
        if (!session) return send(failed_reply(request.req_id, "Resume", {FailureStep::Resume, std::nullopt}));
        if (auto resumed = session->resume(); !resumed)
            return send(failed_reply(request.req_id, "Resume", from_spawn(resumed.error(), FailureStep::Resume)));
        send(common::CommandResult{request.req_id, true, std::nullopt});
    }

    void handle(const wh::Inject& request) {
        if (!session) return send(failed_reply(request.req_id, "Inject", {FailureStep::Inject, std::nullopt}));
        if (auto injected = session->inject(request.entry); !injected)
            return send(failed_reply(request.req_id, "Inject", {FailureStep::Inject, injected.error().error.code}));
        send(common::CommandResult{request.req_id, true, std::nullopt});
    }

    void handle(const wh::Stop& request) {
        send(common::CommandResult{request.req_id, true, std::nullopt});
        if (session) {
            // The game's own exit, or the kill past grace, reaches the engine as Exited first.
            if (auto stopped = session->stop(std::chrono::milliseconds{request.grace_ms}); !stopped)
                send(to_fatal({FailureStep::Stop, static_cast<i64>(WAIT_TIMEOUT)}));
        }
        finish(WinhostExit::Stopped);
    }

    // Stops taking requests, lets the current one finish and ends the Job; idempotent.
    void close() {
        {
            const std::scoped_lock lock(mutex);
            closing = true;
            queue.clear();
        }
        wake.notify_one();
        if (worker.joinable()) worker.join();
        session.reset();
    }

    [[nodiscard]] std::optional<WinhostExit> final_outcome() {
        const std::scoped_lock lock(mutex);
        return outcome;
    }
};

WinhostAgent::WinhostAgent(ControlConnection& connection) : impl_(std::make_unique<Impl>(connection)) {}

WinhostAgent::~WinhostAgent() { impl_->close(); }

Expected<void> WinhostAgent::hello(const ControlTokenBytes& token) {
    wh::WhHello message{token.reveal(), REBOOT_WINHOST_BUILD, wh::kWinhostProtocol,
                        static_cast<u32>(GetCurrentProcessId())};
    auto sent = impl_->connection.send(message);
    secure_wipe(message.token.data(), message.token.size());
    if (!sent) return std::unexpected(WinhostFailure{FailureStep::Handshake, sent.error().os_code});
    return {};
}

WinhostExit WinhostAgent::run() {
    Impl& impl = *impl_;
    bool welcomed = false;
    std::optional<WinhostExit> ended;

    while (!ended) {
        auto frame = impl.connection.next_frame();
        if (!frame) {
            ended = frame.error().step == FailureStep::Protocol ? WinhostExit::ProtocolError
                    : welcomed                                  ? WinhostExit::EngineClosed
                                                                : WinhostExit::Refused;
            break;
        }
        if (!*frame) {
            ended = welcomed ? WinhostExit::EngineClosed : WinhostExit::Refused;
            break;
        }
        const ControlFrame& received = **frame;

        if (received.type == contract_frame_type_v<common::Ping>) {
            const auto ping = decode_frame<common::Ping>(received);
            if (!ping) ended = WinhostExit::ProtocolError;
            else impl.send(common::Pong{ping->nonce});
        } else if (received.type == contract_frame_type_v<wh::WhWelcome>) {
            auto welcome = decode_frame<wh::WhWelcome>(received);
            if (!welcome || welcomed) {
                if (welcome) wipe_secrets(welcome->spawn);
                ended = WinhostExit::ProtocolError;
                continue;
            }
            welcomed = true;
            LaunchRequest launch{std::move(welcome->spawn)};
            impl.worker = std::thread([&impl] { impl.serve(); });
            impl.enqueue(std::move(launch));
        } else if (received.type == contract_frame_type_v<wh::Resume>) {
            const auto request = decode_frame<wh::Resume>(received);
            if (!request || !welcomed) ended = WinhostExit::ProtocolError;
            else impl.enqueue(*request);
        } else if (received.type == contract_frame_type_v<wh::Inject>) {
            auto request = decode_frame<wh::Inject>(received);
            if (!request || !welcomed) ended = WinhostExit::ProtocolError;
            else impl.enqueue(std::move(*request));
        } else if (received.type == contract_frame_type_v<wh::Stop>) {
            const auto request = decode_frame<wh::Stop>(received);
            if (!request || !welcomed) ended = WinhostExit::ProtocolError;
            else impl.enqueue(*request);
        } else {
            RequestId id;
            static_cast<void>(sb::wire::decode(received.payload.reveal(), id));
            impl.send(common::Unsupported{id.req_id});
        }
    }

    if (*ended == WinhostExit::ProtocolError) impl.send(to_fatal({FailureStep::Protocol, std::nullopt}));
    impl.close();
    impl.connection.shutdown();
    return impl.final_outcome().value_or(*ended);
}

WinhostExit run_winhost() noexcept {
    try {
        auto bootstrap = read_bootstrap();
        if (!bootstrap) {
            std::fputs("reboot-winhost: REBOOT_CTL or REBOOT_CTL_TOKEN is missing or malformed\n", stderr);
            return WinhostExit::BadBootstrap;
        }
        auto connection = ControlConnection::connect(bootstrap->port);
        if (!connection) {
            std::fprintf(stderr, "reboot-winhost: cannot connect to the engine on port %u (error %lld)\n",
                         static_cast<unsigned>(bootstrap->port),
                         static_cast<long long>(connection.error().os_code.value_or(0)));
            return WinhostExit::ConnectFailed;
        }
        WinhostAgent agent(**connection);
        if (auto said = agent.hello(bootstrap->token); !said) {
            std::fprintf(stderr, "reboot-winhost: cannot send Hello (error %lld)\n",
                         static_cast<long long>(said.error().os_code.value_or(0)));
            return WinhostExit::ConnectFailed;
        }
        return agent.run();
    } catch (...) {
        std::fputs("reboot-winhost: internal error\n", stderr);
        return WinhostExit::InternalError;
    }
}

}  // namespace reboot::os_windows::winhost
