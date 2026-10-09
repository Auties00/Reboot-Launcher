#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "reboot/contracts/backend.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/testing/fake_backend_script.hpp"
#include "reboot/testing/stdio_peer.hpp"

namespace boost::asio {
class io_context;
}

namespace rb::testing {

// Covers no capability ids (decisions testing-strategy, backend-architecture).
// The program side of contracts/backend.hpp; unscripted it is a conforming backend. It runs
// in-process behind ScriptedProcessLauncher and as reboot-fake-backend.
class FakeBackend final : public IStdioPeer {
public:
    // The script must not serve HTTP.
    FakeBackend(Executor& executor, const IClock& clock, FakeBackendScript script);
    // Serves the script's HTTP on `http_io`.
    FakeBackend(Executor& executor, const IClock& clock, FakeBackendScript script, boost::asio::io_context& http_io);
    ~FakeBackend() override;
    FakeBackend(const FakeBackend&) = delete;
    FakeBackend& operator=(const FakeBackend&) = delete;

    void start(StdioPeerOutputs outputs) override;
    void on_stdin(std::span<const u8> bytes) override;
    void on_stdin_eof() override;

    [[nodiscard]] std::optional<contracts::backend::BackendWelcome> welcome() const;
    [[nodiscard]] std::vector<contracts::backend::RegisterAccount> registered_accounts() const;
    [[nodiscard]] std::vector<contracts::backend::ConfigureSession> live_sessions() const;
    [[nodiscard]] std::size_t credentials_minted() const;
    // The engine's answers to match_target_requests.
    [[nodiscard]] std::vector<contracts::backend::MatchTarget> match_targets() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// reboot-fake-backend `--control=stdio [--script=<file>]`; without --script it reads
// `<its own path>.script.json` when present, so a copy can stand in for reboot-backend.
int fake_backend_main(int argc, char** argv);

}  // namespace rb::testing
