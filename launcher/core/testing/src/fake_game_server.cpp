#include "reboot/testing/fake_game_server.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/post.hpp>

#include "peer_process.hpp"
#include "raw_stdio.hpp"
#include "stdio_peer_core.hpp"

namespace reboot::testing {
namespace {

namespace asio = boost::asio;
using asio::ip::udp;
namespace gs = contracts::game_server;

// The error ListenFailed reports for a scripted failure: EADDRINUSE's Windows value.
constexpr i64 kScriptedBindError = 10048;
// How long closing waits for a socket thread that may not be running.
constexpr std::chrono::seconds kCloseWait{1};

// The bound block; the game socket answers the rbsb/1 probe by echoing it.
class SocketBlock : public std::enable_shared_from_this<SocketBlock> {
public:
    explicit SocketBlock(asio::io_context& io) : io_(io) {}

    // No SO_REUSEADDR: a port someone holds fails, and the error is the OS's own.
    std::optional<std::pair<u16, i64>> bind(const std::string& address, std::span<const u16> ports) {
        boost::system::error_code error;
        const auto ip = asio::ip::make_address(address, error);
        for (const u16 port : ports) {
            auto bound = std::make_unique<Bound>(io_);
            if (!error) bound->socket.open(ip.is_v6() ? udp::v6() : udp::v4(), error);
            if (!error) bound->socket.bind(udp::endpoint(ip, port), error);
            if (error) return std::pair{port, static_cast<i64>(error.value())};
            sockets_.push_back(std::move(bound));
        }
        return std::nullopt;
    }

    void answer_probes(std::size_t index) {
        asio::post(io_, [self = shared_from_this(), index] { self->receive(index); });
    }

    // Closed on the socket thread, and waited for, so a respawn can bind the same block at once.
    void close() {
        auto closed = std::make_shared<std::promise<void>>();
        std::future<void> done = closed->get_future();
        asio::post(io_, [self = shared_from_this(), closed] {
            boost::system::error_code ignored;
            for (const auto& bound : self->sockets_) bound->socket.close(ignored);
            closed->set_value();
        });
        if (!io_.get_executor().running_in_this_thread()) (void)done.wait_for(kCloseWait);
    }

private:
    // Each socket reads into its own buffer, since several may wait at once.
    struct Bound {
        explicit Bound(asio::io_context& io) : socket(io) {}
        udp::socket socket;
        std::array<u8, 512> buffer{};
        udp::endpoint from;
    };

    void receive(std::size_t index) {
        Bound& bound = *sockets_[index];
        bound.socket.async_receive_from(
            asio::buffer(bound.buffer), bound.from,
            [self = shared_from_this(), index](const boost::system::error_code& error, std::size_t n) {
                Bound& on = *self->sockets_[index];
                if (error == asio::error::operation_aborted || !on.socket.is_open()) return;
                if (!error && n == gs::kRbsbProbe.size() &&
                    std::equal(gs::kRbsbProbe.begin(), gs::kRbsbProbe.end(), on.buffer.begin())) {
                    boost::system::error_code ignored;
                    on.socket.send_to(asio::buffer(gs::kRbsbProbe), on.from, 0, ignored);
                }
                self->receive(index);
            });
    }

    asio::io_context& io_;
    std::vector<std::unique_ptr<Bound>> sockets_;
};

class ServerCore final : public StdioPeerCore {
public:
    ServerCore(Executor& executor, const IClock& clock, FakeGameServerScript script, asio::io_context* socket_io)
        : StdioPeerCore(executor, clock, script.child), script_(std::move(script)), socket_io_(socket_io) {}

    ~ServerCore() override { release_sockets(); }

    ServerCore(const ServerCore&) = delete;
    ServerCore& operator=(const ServerCore&) = delete;

    void release_sockets() {
        if (sockets_) sockets_->close();
        sockets_.reset();
    }

    mutable std::mutex mutex;
    std::optional<gs::ServerConfig> config;
    std::optional<gs::MatchState> state;
    std::vector<gs::Ban> bans;
    std::vector<std::string> cidrs;
    std::vector<std::string> commands;
    std::vector<u32> kicked;
    UniqueFunction<void(const gs::Listening&)> on_listening;

private:
    void on_finished() override { release_sockets(); }

    void send_hello() override { send(gs::ServerHello{script_.hello_description.value_or(script_.description)}); }

    void handle(const OwnedFrame& frame) override {
        bool welcomed = false;
        {
            const std::scoped_lock lock(mutex);
            welcomed = config.has_value();
        }
        if (!welcomed) {
            if (frame.type != contract_frame_type_v<gs::ServerWelcome>) return;
            auto welcome = decode_contract<gs::ServerWelcome>(frame.payload);
            if (!welcome) {
                finish(kBadInvocationExitCode);
                return;
            }
            {
                const std::scoped_lock lock(mutex);
                config = welcome->config;
            }
            reached(ScriptStage::Welcome);
            after(script_.listen_delay, [this, listen = std::move(welcome->config.listen)] { start_listening(listen); });
            return;
        }
        dispatch(frame);
    }

    void listen_failed(u16 port, i64 os_error) {
        send(gs::ListenFailed{port, os_error, gs::ListenStage::Bind});
        finish(kBindFailureExitCode);
    }

    void start_listening(const gs::ListenConfig& listen) {
        const std::vector<gs::SocketSpec>& sockets = script_.description.sockets;
        if (listen.ports.size() != sockets.size()) {
            send(gs::Fatal{"bad_config", "Welcome names " + std::to_string(listen.ports.size()) + " ports for " +
                                             std::to_string(sockets.size()) + " sockets"});
            finish(kBadInvocationExitCode);
            return;
        }
        if (script_.bind_failure_index && *script_.bind_failure_index < listen.ports.size()) {
            listen_failed(listen.ports[*script_.bind_failure_index], kScriptedBindError);
            return;
        }
        if (script_.bind_sockets) {
            if (socket_io_ == nullptr) {
                finish(kBadInvocationExitCode);
                return;
            }
            sockets_ = std::make_shared<SocketBlock>(*socket_io_);
            if (const auto failed = sockets_->bind(listen.bind_address, listen.ports)) {
                release_sockets();
                listen_failed(failed->first, failed->second);
                return;
            }
            for (std::size_t i = 0; i < sockets.size(); ++i)
                if (sockets[i].role == gs::SocketRole::Game) sockets_->answer_probes(i);
        }
        gs::Listening listening;
        for (std::size_t i = 0; i < sockets.size(); ++i) listening.bound.push_back({sockets[i].role, listen.ports[i]});
        send(listening);
        if (on_listening) on_listening(listening);
        move_to(gs::MatchState::Lobby);
        reached(ScriptStage::Ready);
        for (const TimedServerEvent& timed : script_.events)
            after(timed.at, [this, event = timed.event] {
                std::visit([this](const auto& message) { send(message); }, event);
            });
    }

    void move_to(gs::MatchState next) {
        {
            const std::scoped_lock lock(mutex);
            state = next;
        }
        send(gs::StateChanged{next});
    }

    void end_match(gs::MatchEndReason reason) {
        ++match_;
        move_to(gs::MatchState::Ending);
        send(gs::MatchEnded{reason, std::nullopt, {}});
    }

    [[nodiscard]] bool in_match() const {
        const std::scoped_lock lock(mutex);
        return state == gs::MatchState::InProgress || state == gs::MatchState::Warmup;
    }

    template <ContractMessage T>
    [[nodiscard]] std::optional<T> decoded(const OwnedFrame& frame) {
        auto message = decode_contract<T>(frame.payload);
        if (message) return std::move(*message);
        const u64 req_id = first_req_id(frame.payload);
        reply([this, req_id, error = std::move(message.error())] { refuse(req_id, error); });
        return std::nullopt;
    }

    template <class T>
    void record(std::vector<T>& into, T value) {
        const std::scoped_lock lock(mutex);
        into.push_back(std::move(value));
    }

    void dispatch(const OwnedFrame& frame) {
        const u64 type = frame.type;
        if (type == contract_frame_type_v<gs::StartMatch>) {
            if (auto request = decoded<gs::StartMatch>(frame))
                reply([this, id = request->req_id] {
                    move_to(gs::MatchState::InProgress);
                    if (script_.match_length)
                        after(*script_.match_length, [this, match = match_] {
                            if (match == match_ && in_match()) end_match(gs::MatchEndReason::Completed);
                        });
                    ok(id);
                });
        } else if (type == contract_frame_type_v<gs::EndMatch>) {
            if (auto request = decoded<gs::EndMatch>(frame))
                reply([this, id = request->req_id] {
                    if (in_match()) end_match(gs::MatchEndReason::Operator);
                    ok(id);
                });
        } else if (type == contract_frame_type_v<gs::Reset>) {
            if (auto request = decoded<gs::Reset>(frame))
                reply([this, id = request->req_id] {
                    // The bound block stays; only the match starts over.
                    ++match_;
                    move_to(gs::MatchState::Lobby);
                    ok(id);
                });
        } else if (type == contract_frame_type_v<gs::Kick>) {
            if (auto request = decoded<gs::Kick>(frame)) {
                record(kicked, request->player_id);
                reply([this, id = request->req_id] { ok(id); });
            }
        } else if (type == contract_frame_type_v<gs::SetBans>) {
            if (auto request = decoded<gs::SetBans>(frame)) {
                {
                    const std::scoped_lock lock(mutex);
                    bans = request->bans;
                }
                reply([this, id = request->req_id] { ok(id); });
            }
        } else if (type == contract_frame_type_v<gs::SetOperators>) {
            if (auto request = decoded<gs::SetOperators>(frame)) {
                {
                    const std::scoped_lock lock(mutex);
                    cidrs = request->ip_cidrs;
                }
                reply([this, id = request->req_id] { ok(id); });
            }
        } else if (type == contract_frame_type_v<gs::RunCommand>) {
            if (auto request = decoded<gs::RunCommand>(frame)) {
                record(commands, request->text);
                reply([this, id = request->req_id] { ok(id); });
            }
        } else if (type == contract_frame_type_v<gs::Drain>) {
            if (auto request = decoded<gs::Drain>(frame)) reply([this, id = request->req_id] { ok(id); });
        } else if (type == contract_frame_type_v<gs::Shutdown>) {
            if (auto request = decoded<gs::Shutdown>(frame))
                reply([this, id = request->req_id] {
                    ok(id);
                    finish(0);
                });
        } else {
            const u64 req_id = first_req_id(frame.payload);
            reply([this, req_id] { unsupported(req_id); });
        }
    }

    FakeGameServerScript script_;
    asio::io_context* socket_io_;
    std::shared_ptr<SocketBlock> sockets_;
    // Bumped by every end and reset, so a timer from an earlier match does nothing.
    u64 match_ = 0;
};

}  // namespace

struct FakeGameServer::Impl {
    std::shared_ptr<ServerCore> core;
};

FakeGameServer::FakeGameServer(Executor& executor, const IClock& clock, FakeGameServerScript script)
    : impl_(std::make_unique<Impl>(Impl{std::make_shared<ServerCore>(executor, clock, std::move(script), nullptr)})) {}

FakeGameServer::FakeGameServer(Executor& executor, const IClock& clock, FakeGameServerScript script,
                               boost::asio::io_context& socket_io)
    : impl_(std::make_unique<Impl>(Impl{std::make_shared<ServerCore>(executor, clock, std::move(script), &socket_io)})) {}

FakeGameServer::~FakeGameServer() {
    impl_->core->detach();
    impl_->core->release_sockets();
}

void FakeGameServer::start(StdioPeerOutputs outputs) { impl_->core->start(std::move(outputs)); }

void FakeGameServer::on_stdin(std::span<const u8> bytes) { impl_->core->on_stdin(bytes); }

void FakeGameServer::on_stdin_eof() { impl_->core->on_stdin_eof(); }

void FakeGameServer::on_listening(UniqueFunction<void(const contracts::game_server::Listening&)> hook) {
    impl_->core->on_listening = std::move(hook);
}

std::optional<contracts::game_server::ServerConfig> FakeGameServer::config() const {
    const std::scoped_lock lock(impl_->core->mutex);
    return impl_->core->config;
}

std::optional<contracts::game_server::MatchState> FakeGameServer::state() const {
    const std::scoped_lock lock(impl_->core->mutex);
    return impl_->core->state;
}

std::vector<contracts::game_server::Ban> FakeGameServer::bans() const {
    const std::scoped_lock lock(impl_->core->mutex);
    return impl_->core->bans;
}

std::vector<std::string> FakeGameServer::operator_cidrs() const {
    const std::scoped_lock lock(impl_->core->mutex);
    return impl_->core->cidrs;
}

std::vector<std::string> FakeGameServer::commands() const {
    const std::scoped_lock lock(impl_->core->mutex);
    return impl_->core->commands;
}

std::vector<u32> FakeGameServer::kicked() const {
    const std::scoped_lock lock(impl_->core->mutex);
    return impl_->core->kicked;
}

std::vector<u8> describe_frame(const FakeGameServerScript& script) { return encode_contract_frame(script.description); }

int fake_game_server_main(int argc, char** argv) {
    auto args = parse_peer_arguments(argc, argv, true);
    if (!args) return kBadInvocationExitCode;
    FakeGameServerScript script;
    if (args->script) {
        auto loaded = load_fake_game_server_script(*args->script);
        if (!loaded) {
            report_bad_script(loaded.error());
            return kBadInvocationExitCode;
        }
        script = std::move(*loaded);
    }
    if (args->describe) {
        if (script.describe_exit_code) return *script.describe_exit_code;
        raw_stdio::make_binary();
        const std::vector<u8> frame = describe_frame(script);
        return raw_stdio::write_all(1, frame.data(), static_cast<unsigned long>(frame.size())) ? 0 : 1;
    }
    run_peer_process([&script](Executor& executor, const IClock& clock, boost::asio::io_context& io) {
        return std::unique_ptr<IStdioPeer>(std::make_unique<FakeGameServer>(executor, clock, std::move(script), io));
    });
}

}  // namespace reboot::testing
