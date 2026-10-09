#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/testing/fake_client_dll_script.hpp"
#include "reboot/testing/frame_log.hpp"
#include "reboot/testing/game_control_bootstrap.hpp"

namespace boost::asio {
class io_context;
}

namespace rb::testing {

// Covers no capability ids (decisions testing-strategy, game-control-channel).
// Plays our in-game client DLL against the engine's game channel: preamble, GcHello, then the script
// once GcWelcome arrives. Thread-safe: socket callbacks run on the io_context, pauses on `executor`.
class FakeClientDll {
public:
    FakeClientDll(Executor& executor, const IClock& clock, FakeClientDllScript script);
    ~FakeClientDll();
    FakeClientDll(const FakeClientDll&) = delete;
    FakeClientDll& operator=(const FakeClientDll&) = delete;

    // Real loopback TCP to the bootstrap's endpoint, as the DLL connects from inside the game.
    Result<void> connect(boost::asio::io_context& io, const GameControlBootstrap& bootstrap);
    // An already connected stream, e.g. one end of a memory pair whose other end the engine adopts.
    void attach(std::unique_ptr<ports::IByteStream> stream, const GameControlBootstrap& bootstrap);

    template <ContractMessage T>
    void send(const T& message) {
        send_frame(encode_contract_frame(message));
    }
    void disconnect();

    [[nodiscard]] std::optional<contracts::game_client::ClientDllConfig> welcome() const;
    [[nodiscard]] std::vector<OwnedFrame> received_frames() const;
    // Every frame of T, or the first one's decode error.
    template <ContractMessage T>
    [[nodiscard]] Result<std::vector<T>> received() const {
        std::vector<T> out;
        for (const OwnedFrame& frame : received_frames()) {
            if (frame.type != contract_frame_type_v<T>) continue;
            Result<T> message = decode_contract<T>(frame.payload);
            if (!message) return std::unexpected(std::move(message.error()));
            out.push_back(std::move(*message));
        }
        return out;
    }
    // TestJoin and TestQuit are answered only when GcWelcome set test_mode, Unsupported otherwise.
    [[nodiscard]] std::vector<std::string> test_joins() const;
    [[nodiscard]] bool quit_requested() const;
    // Ping gets Pong until the script's ScriptStopPonging.
    [[nodiscard]] std::size_t pings_answered() const;
    [[nodiscard]] bool connected() const;
    // The engine closed the connection, or the script disconnected.
    [[nodiscard]] bool closed() const;

private:
    void send_frame(std::vector<u8> frame);

    struct Impl;
    // Shared with socket handlers and posted steps, which may outlive this object.
    std::shared_ptr<Impl> impl_;
};

}  // namespace rb::testing
