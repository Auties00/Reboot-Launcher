#include "reboot/testing/fake_winhost.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "current_process.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/testing/fake_client_dll.hpp"
#include "stdio_peer_core.hpp"
#include "tcp_stream.hpp"

namespace reboot::testing {
namespace {

namespace wh = contracts::winhost;
namespace common = contracts::common;

// The file name of a UTF-16LE Windows path, as UTF-8.
[[nodiscard]] std::string file_name_of(const wh::Bytes& path_utf16) {
    std::u16string text;
    for (std::size_t i = 0; i + 1 < path_utf16.size(); i += 2)
        text.push_back(static_cast<char16_t>(path_utf16[i] | path_utf16[i + 1] << 8));
    const std::size_t slash = text.find_last_of(u"\\/");
    return utf16_to_utf8(slash == std::u16string::npos ? text : text.substr(slash + 1));
}

}  // namespace

struct FakeWinhost::Impl : std::enable_shared_from_this<FakeWinhost::Impl> {
    Impl(Executor& executor_ref, const IClock& clock_ref, FakeWinhostScript host_script)
        : executor(executor_ref), clock(clock_ref), script(std::move(host_script)) {}

    Executor& executor;
    const IClock& clock;
    FakeWinhostScript script;
    // Set by connect(), so our DLL in the game connects the same way.
    boost::asio::io_context* io = nullptr;

    mutable std::mutex mutex;
    std::shared_ptr<ports::IByteStream> stream;
    GameControlBootstrap bootstrap;
    FrameLog received{kGameControlFrameCap};
    std::size_t handled = 0;
    std::optional<wh::SpawnGame> spawn;
    std::vector<wh::InjectSpec> injected;
    std::optional<u32> stop_grace;
    std::unique_ptr<FakeClientDll> dll;
    bool resumed = false;
    bool stop_ponging = false;
    bool job_terminated = false;
    bool closed = false;

    void write(std::vector<u8> frame) {
        std::shared_ptr<ports::IByteStream> target;
        {
            const std::scoped_lock lock(mutex);
            if (closed) return;
            target = stream;
        }
        if (target) target->write(frame);
    }

    template <ContractMessage T>
    void send(const T& message) {
        write(encode_contract_frame(message));
    }

    void later(std::chrono::milliseconds delay, UniqueFunction<void()> step) {
        auto guarded = [weak = weak_from_this(), step = std::move(step)]() mutable {
            if (const auto self = weak.lock()) step();
        };
        if (delay <= std::chrono::milliseconds::zero()) {
            executor.post(std::move(guarded));
        } else {
            executor.post_at(clock.steady_now() + delay, std::move(guarded));
        }
    }

    void begin(std::shared_ptr<ports::IByteStream> connected, const GameControlBootstrap& boot) {
        {
            const std::scoped_lock lock(mutex);
            stream = connected;
            bootstrap = boot;
        }
        const std::weak_ptr<Impl> weak = weak_from_this();
        connected->on_read([weak](std::span<const u8> bytes) {
            if (const auto self = weak.lock()) self->on_bytes(bytes);
        });
        // EOF from the engine: winhost terminates its Job, which takes the game with it.
        connected->on_close([weak] {
            if (const auto self = weak.lock()) self->terminate_job();
        });
        connected->write(game_control_preamble(script.payload_abi));
        if (!script.send_hello) return;
        later(script.hello_delay, [this] {
            send(wh::WhHello{script.token_override.value_or(bootstrap.token), script.build, script.protocol,
                             current_process_id()});
        });
    }

    void on_bytes(std::span<const u8> bytes) {
        std::vector<OwnedFrame> fresh;
        {
            const std::scoped_lock lock(mutex);
            (void)received.feed(bytes);
            const auto& frames = received.frames();
            for (; handled < frames.size(); ++handled) fresh.push_back(frames[handled]);
        }
        for (const OwnedFrame& frame : fresh) handle(frame);
    }

    [[nodiscard]] wh::Injected injection_of(const wh::InjectSpec& entry) const {
        const bool fails = std::ranges::contains(script.failing_injections, file_name_of(entry.path_utf16));
        return wh::Injected{entry.path_utf16, !fails, fails ? std::optional<i64>(kFakeInjectionError) : std::nullopt};
    }

    void handle(const OwnedFrame& frame) {
        if (frame.type == contract_frame_type_v<common::Ping>) {
            const auto ping = decode_contract<common::Ping>(frame.payload);
            {
                const std::scoped_lock lock(mutex);
                if (!ping || stop_ponging) return;
            }
            send(common::Pong{ping->nonce});
        } else if (frame.type == contract_frame_type_v<wh::WhWelcome>) {
            auto welcomed = decode_contract<wh::WhWelcome>(frame.payload);
            if (!welcomed) return;
            {
                const std::scoped_lock lock(mutex);
                if (spawn) return;
                spawn = welcomed->spawn;
            }
            if (script.auto_spawn) spawn_game(welcomed->spawn);
        } else if (frame.type == contract_frame_type_v<wh::Resume>) {
            const auto request = decode_contract<wh::Resume>(frame.payload);
            if (!request) return;
            {
                const std::scoped_lock lock(mutex);
                resumed = true;
            }
            send(common::CommandResult{request->req_id, true, std::nullopt});
            start_game_dll();
            later(std::chrono::milliseconds::zero(), [this] { run_steps(0); });
        } else if (frame.type == contract_frame_type_v<wh::Inject>) {
            const auto request = decode_contract<wh::Inject>(frame.payload);
            if (!request) return;
            {
                const std::scoped_lock lock(mutex);
                injected.push_back(request->entry);
            }
            send(injection_of(request->entry));
            send(common::CommandResult{request->req_id, true, std::nullopt});
        } else if (frame.type == contract_frame_type_v<wh::Stop>) {
            const auto request = decode_contract<wh::Stop>(frame.payload);
            if (!request) return;
            {
                const std::scoped_lock lock(mutex);
                stop_grace = request->grace_ms;
            }
            send(common::CommandResult{request->req_id, true, std::nullopt});
            send(wh::Exited{wh::ProcessRole::Game, script.exit_code_on_stop});
            disconnect();
        } else if (frame.type != contract_frame_type_v<common::Pong> && frame.type != contract_frame_type_v<common::CommandResult>) {
            send(common::Unsupported{first_req_id(frame.payload)});
        }
    }

    void spawn_game(const wh::SpawnGame& game) {
        u32 pid = script.first_pid;
        send(wh::Spawned{wh::ProcessRole::Game, pid});
        for (std::size_t i = 0; i < game.companions.size(); ++i) send(wh::Spawned{wh::ProcessRole::Companion, ++pid});
        for (const wh::InjectSpec& entry : game.inject) {
            if (entry.phase != wh::InjectPhase::Early) continue;
            {
                const std::scoped_lock lock(mutex);
                injected.push_back(entry);
            }
            send(injection_of(entry));
        }
    }

    void start_game_dll() {
        if (!script.game_client_dll) return;
        std::optional<wh::SpawnGame> game;
        {
            const std::scoped_lock lock(mutex);
            if (dll) return;
            game = spawn;
        }
        if (!game) return;
        auto boot = read_game_control_bootstrap(game->env_block_utf16);
        auto started = std::make_unique<FakeClientDll>(executor, clock, *script.game_client_dll);
        // Over a memory stream there is no socket to reach; the test then attaches the DLL itself.
        if (boot && io != nullptr) (void)started->connect(*io, *boot);
        const std::scoped_lock lock(mutex);
        dll = std::move(started);
    }

    void run_steps(std::size_t index) {
        for (; index < script.after_resume.size(); ++index) {
            if (is_closed()) return;
            const WinhostStep& step = script.after_resume[index];
            if (const auto* pause = std::get_if<ScriptPause>(&step)) {
                later(pause->duration, [this, next = index + 1] { run_steps(next); });
                return;
            }
            if (std::holds_alternative<ScriptDisconnect>(step)) {
                disconnect();
                return;
            }
            if (std::holds_alternative<ScriptStopPonging>(step)) {
                const std::scoped_lock lock(mutex);
                stop_ponging = true;
                continue;
            }
            std::visit(
                [this]<class M>(const M& message) {
                    if constexpr (ContractMessage<M>) send(message);
                },
                step);
        }
    }

    [[nodiscard]] bool is_closed() const {
        const std::scoped_lock lock(mutex);
        return closed;
    }

    // The Job goes with winhost, and the game and our DLL in it.
    void terminate_job() {
        FakeClientDll* game_dll = nullptr;
        {
            const std::scoped_lock lock(mutex);
            closed = true;
            job_terminated = true;
            game_dll = dll.get();
        }
        if (game_dll != nullptr) game_dll->disconnect();
    }

    void disconnect() {
        std::shared_ptr<ports::IByteStream> target;
        {
            const std::scoped_lock lock(mutex);
            if (closed) return;
            target = stream;
        }
        if (target) target->close();
        terminate_job();
    }
};

FakeWinhost::FakeWinhost(Executor& executor, const IClock& clock, FakeWinhostScript script)
    : impl_(std::make_shared<Impl>(executor, clock, std::move(script))) {}

FakeWinhost::~FakeWinhost() { impl_->disconnect(); }

Result<void> FakeWinhost::connect(boost::asio::io_context& io, const GameControlBootstrap& bootstrap) {
    auto stream = connect_tcp(io, bootstrap.engine);
    if (!stream) return std::unexpected(std::move(stream.error()));
    impl_->io = &io;
    impl_->begin(std::shared_ptr<ports::IByteStream>(std::move(*stream)), bootstrap);
    return {};
}

void FakeWinhost::attach(std::unique_ptr<ports::IByteStream> stream, const GameControlBootstrap& bootstrap) {
    impl_->begin(std::shared_ptr<ports::IByteStream>(std::move(stream)), bootstrap);
}

Result<GameControlBootstrap> FakeWinhost::bootstrap_of(const ports::ProcessLaunch& runner_launch) {
    return read_game_control_bootstrap(runner_launch.env);
}

void FakeWinhost::game_exits(i64 code) { impl_->send(contracts::winhost::Exited{contracts::winhost::ProcessRole::Game, code}); }

void FakeWinhost::disconnect() { impl_->disconnect(); }

void FakeWinhost::send_frame(std::vector<u8> frame) { impl_->write(std::move(frame)); }

std::optional<contracts::winhost::SpawnGame> FakeWinhost::spawn_request() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->spawn;
}

bool FakeWinhost::resumed() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->resumed;
}

std::vector<contracts::winhost::InjectSpec> FakeWinhost::injected() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->injected;
}

std::optional<u32> FakeWinhost::stop_grace_ms() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->stop_grace;
}

bool FakeWinhost::job_terminated() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->job_terminated;
}

std::vector<OwnedFrame> FakeWinhost::received_frames() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->received.frames();
}

FakeClientDll* FakeWinhost::game_client_dll() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->dll.get();
}

}  // namespace reboot::testing
