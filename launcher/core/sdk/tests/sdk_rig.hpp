#pragma once

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "engine_rig.hpp"
#include "reboot/api/v1/host.hpp"
#include "reboot/api/v1/requests.hpp"
#include "reboot/base/native_path.hpp"
#include "reboot/ipc/endpoint.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/ports/platform_services.hpp"
#include "reboot/sdk/sdk.hpp"

namespace rb::sdk::test {

using namespace std::chrono_literals;

// A stream the test can cut, as a transient pipe or socket error would.
class DroppableStream final : public ports::IByteStream {
public:
    explicit DroppableStream(std::shared_ptr<ports::IByteStream> inner) : inner_(std::move(inner)) {}

    void write(std::span<const u8> bytes) override { inner_->write(bytes); }
    void on_read(UniqueFunction<void(std::span<const u8>)> callback) override { inner_->on_read(std::move(callback)); }
    void on_close(UniqueFunction<void()> callback) override { inner_->on_close(std::move(callback)); }
    void close() override { inner_->close(); }
    [[nodiscard]] ports::PeerIdentity peer() const override { return inner_->peer(); }

private:
    std::shared_ptr<ports::IByteStream> inner_;
};

// The OS's real engine endpoint, whose accepted connections drop_all() cuts.
class DroppableListener final : public ports::IIpcListener {
public:
    explicit DroppableListener(std::unique_ptr<ports::IIpcListener> inner) : inner_(std::move(inner)) {}

    Result<void> listen(std::string_view endpoint_name,
                        UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> on_accept) override {
        return inner_->listen(endpoint_name, [this, accept = std::move(on_accept)](std::unique_ptr<ports::IByteStream> stream) mutable {
            std::shared_ptr<ports::IByteStream> shared = std::move(stream);
            {
                const std::scoped_lock lock(mutex_);
                streams_.push_back(shared);
            }
            accept(std::make_unique<DroppableStream>(std::move(shared)));
        });
    }
    void close() override { inner_->close(); }

    void drop_all() {
        std::vector<std::weak_ptr<ports::IByteStream>> streams;
        {
            const std::scoped_lock lock(mutex_);
            streams.swap(streams_);
        }
        for (const std::weak_ptr<ports::IByteStream>& weak : streams)
            if (const std::shared_ptr<ports::IByteStream> stream = weak.lock()) stream->close();
    }

private:
    std::unique_ptr<ports::IIpcListener> inner_;
    std::mutex mutex_;
    std::vector<std::weak_ptr<ports::IByteStream>> streams_;
};

// The engine rig behind the OS's real endpoint, reached through reboot_client. The C calls block
// the test thread, so a thread of its own pumps the engine strand.
class SdkRig {
public:
    SdkRig() {
        Result<ports::EngineEndpoint> endpoint = ports::make_engine_endpoint();
        REQUIRE(endpoint);
        auto listener = std::make_unique<DroppableListener>(std::move(endpoint->listener));
        listener_ = listener.get();
        rig.platform.ipc_listener = std::move(listener);
        Result<std::string> name = ipc::endpoint_for(endpoint->self, rig.info.root_hash16);
        REQUIRE(name);
        rig.info.endpoint = std::move(*name);
        rig.serve_backend();
        rig.serve_game_server();
        rig.boot();
        pump_ = std::thread([this] {
            while (!stopping_.load()) static_cast<void>(rig.strand.pump_until([this] { return stopping_.load(); }, 24h, 1h));
        });
    }

    ~SdkRig() {
        stopping_.store(true);
        pump_.join();
    }

    SdkRig(const SdkRig&) = delete;
    SdkRig& operator=(const SdkRig&) = delete;

    [[nodiscard]] ConnectOptions options(LaunchMode mode = LaunchMode::Autostart) const {
        ConnectOptions out;
        out.data_root = display_utf8(rig.root.root);
        out.client_kind = ClientKind::Test;
        out.launch_mode = mode;
        out.connect_deadline = 10s;
        return out;
    }

    [[nodiscard]] Context connect(LaunchMode mode = LaunchMode::Autostart) const {
        Expected<Context> context = Context::connect(options(mode));
        if (!context) FAIL(context.error().id() << ": " << context.error().english());
        return std::move(*context);
    }

    // Cuts every client connection; Autostart clients reconnect to the same engine.
    void drop_connections() { listener_->drop_all(); }

    [[nodiscard]] static api::HostProfile profile(const Context& context) {
        Expected<api::HostProfilesCreateResponse> created =
            context.host_profiles_create(api::HostProfilesCreateRequest{engine::test::EngineRig::arena_profile()});
        REQUIRE(created);
        return created->profile;
    }

    // Accepts the first pending request of `kind`, waiting for it to appear.
    static void accept(const Context& context, api::UserRequestKind kind) {
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        while (std::chrono::steady_clock::now() < deadline) {
            Expected<api::RequestsPendingResponse> pending = context.requests_pending();
            REQUIRE(pending);
            for (const api::UserActionRequired& request : pending->requests) {
                if (request.kind != kind) continue;
                api::RequestsRespondRequest respond{request.request_id, engine::test::EngineRig::accept()};
                REQUIRE(context.requests_respond(respond));
                return;
            }
            std::this_thread::sleep_for(20ms);
        }
        FAIL("no pending request of the expected kind");
    }

    engine::test::EngineRig rig;

private:
    DroppableListener* listener_ = nullptr;
    std::atomic<bool> stopping_{false};
    std::thread pump_;
};

}  // namespace rb::sdk::test
