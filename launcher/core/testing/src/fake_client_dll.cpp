#include "reboot/testing/fake_client_dll.hpp"

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
#include "stdio_peer_core.hpp"
#include "tcp_stream.hpp"

namespace rb::testing {
namespace {

namespace gc = contracts::game_client;
namespace common = contracts::common;

}  // namespace

struct FakeClientDll::Impl : std::enable_shared_from_this<FakeClientDll::Impl> {
    Impl(Executor& executor_ref, const IClock& clock_ref, FakeClientDllScript dll_script)
        : executor(executor_ref), clock(clock_ref), script(std::move(dll_script)) {}

    Executor& executor;
    const IClock& clock;
    FakeClientDllScript script;

    mutable std::mutex mutex;
    std::shared_ptr<ports::IByteStream> stream;
    GameControlBootstrap bootstrap;
    FrameLog received{kGameControlFrameCap};
    std::size_t handled = 0;
    std::optional<gc::ClientDllConfig> welcome;
    std::vector<std::string> test_joins;
    bool quit = false;
    std::size_t pings = 0;
    bool stop_ponging = false;
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
        connected->on_close([weak] {
            if (const auto self = weak.lock()) {
                const std::scoped_lock lock(self->mutex);
                self->closed = true;
            }
        });
        const auto preamble = game_control_preamble(script.payload_abi);
        connected->write(preamble);
        if (!script.send_hello) return;
        later(script.hello_delay, [this] {
            gc::GcHello hello;
            hello.token = script.token_override.value_or(bootstrap.token);
            hello.role = gc::PeerRole::ClientDll;
            hello.dll_build = script.dll_build;
            hello.protocol = script.protocol;
            hello.game = script.game;
            hello.exe_sha256 = script.exe_sha256;
            hello.pid = current_process_id();
            send(hello);
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

    void handle(const OwnedFrame& frame) {
        if (frame.type == contract_frame_type_v<common::Ping>) {
            const auto ping = decode_contract<common::Ping>(frame.payload);
            {
                const std::scoped_lock lock(mutex);
                if (!ping || stop_ponging) return;
                ++pings;
            }
            send(common::Pong{ping->nonce});
        } else if (frame.type == contract_frame_type_v<gc::GcWelcome>) {
            auto welcomed = decode_contract<gc::GcWelcome>(frame.payload);
            if (!welcomed) return;
            {
                const std::scoped_lock lock(mutex);
                if (welcome) return;
                welcome = std::move(welcomed->config);
            }
            later(std::chrono::milliseconds::zero(), [this] { run_steps(0); });
        } else if (frame.type == contract_frame_type_v<gc::GcShutdown>) {
            const auto request = decode_contract<gc::GcShutdown>(frame.payload);
            if (!request) return;
            send(common::CommandResult{request->req_id, true, std::nullopt});
            if (script.exit_on_shutdown) disconnect();
        } else if (frame.type == contract_frame_type_v<gc::TestJoin>) {
            const auto request = decode_contract<gc::TestJoin>(frame.payload);
            if (!request) return;
            if (!test_mode()) {
                send(common::Unsupported{request->req_id});
                return;
            }
            {
                const std::scoped_lock lock(mutex);
                test_joins.push_back(request->address);
            }
            send(common::CommandResult{request->req_id, true, std::nullopt});
        } else if (frame.type == contract_frame_type_v<gc::TestQuit>) {
            const auto request = decode_contract<gc::TestQuit>(frame.payload);
            if (!request) return;
            if (!test_mode()) {
                send(common::Unsupported{request->req_id});
                return;
            }
            {
                const std::scoped_lock lock(mutex);
                quit = true;
            }
            send(common::CommandResult{request->req_id, true, std::nullopt});
        } else if (frame.type != contract_frame_type_v<common::Pong> && frame.type != contract_frame_type_v<common::CommandResult>) {
            send(common::Unsupported{first_req_id(frame.payload)});
        }
    }

    [[nodiscard]] bool test_mode() const {
        const std::scoped_lock lock(mutex);
        return welcome && welcome->test_mode;
    }

    void run_steps(std::size_t index) {
        for (; index < script.after_welcome.size(); ++index) {
            if (is_closed()) return;
            const ClientDllStep& step = script.after_welcome[index];
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

    void disconnect() {
        std::shared_ptr<ports::IByteStream> target;
        {
            const std::scoped_lock lock(mutex);
            if (closed) return;
            closed = true;
            target = stream;
        }
        if (target) target->close();
    }
};

FakeClientDll::FakeClientDll(Executor& executor, const IClock& clock, FakeClientDllScript script)
    : impl_(std::make_shared<Impl>(executor, clock, std::move(script))) {}

FakeClientDll::~FakeClientDll() { impl_->disconnect(); }

Result<void> FakeClientDll::connect(boost::asio::io_context& io, const GameControlBootstrap& bootstrap) {
    auto stream = connect_tcp(io, bootstrap.engine);
    if (!stream) return std::unexpected(std::move(stream.error()));
    impl_->begin(std::shared_ptr<ports::IByteStream>(std::move(*stream)), bootstrap);
    return {};
}

void FakeClientDll::attach(std::unique_ptr<ports::IByteStream> stream, const GameControlBootstrap& bootstrap) {
    impl_->begin(std::shared_ptr<ports::IByteStream>(std::move(stream)), bootstrap);
}

void FakeClientDll::disconnect() { impl_->disconnect(); }

void FakeClientDll::send_frame(std::vector<u8> frame) { impl_->write(std::move(frame)); }

std::optional<contracts::game_client::ClientDllConfig> FakeClientDll::welcome() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->welcome;
}

std::vector<OwnedFrame> FakeClientDll::received_frames() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->received.frames();
}

std::vector<std::string> FakeClientDll::test_joins() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->test_joins;
}

bool FakeClientDll::quit_requested() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->quit;
}

std::size_t FakeClientDll::pings_answered() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->pings;
}

bool FakeClientDll::connected() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->stream != nullptr && !impl_->closed;
}

bool FakeClientDll::closed() const { return impl_->is_closed(); }

}  // namespace rb::testing
