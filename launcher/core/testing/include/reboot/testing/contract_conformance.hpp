#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "reboot/contracts/game_server.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/testing/conformance_report.hpp"
#include "reboot/testing/conformance_waiter.hpp"

namespace reboot::testing {

// reboot-fake-backend or reboot-fake-game-server now; reboot-backend and reboot-game-server later.
struct ContractSubject {
    NativePath exe;
    // Before --control=stdio or --describe, e.g. --script=<file> for a fake.
    std::vector<std::string> extra_args;
    ports::EnvBlock env;
    // Also where the backend keeps its data and the game server its logs.
    NativePath work_dir;
};

struct ContractTiming {
    std::chrono::milliseconds hello = default_deadline(OpKind::ChildHello);
    std::chrono::milliseconds ready = default_deadline(OpKind::BackendReady);
    std::chrono::milliseconds reply{5000};
    std::chrono::milliseconds stop_grace = default_deadline(OpKind::GracefulStop);
};

struct GameServerConformanceOptions {
    contracts::game_server::GameSpec game;
    // The first of a free UDP block: one port per declared socket, plus one the suite occupies.
    Port first_port{17777};
};

// Covers no capability ids (decisions testing-strategy, backend-architecture, game-server-dll-design).
// Black-box checks of the child-process contracts across a real process boundary; each check runs
// the subject fresh, so one failure does not mask the next.
class ContractConformance {
public:
    // `launcher` is a real one: the OS conformance run passes its own.
    ContractConformance(ports::IProcessLauncher& launcher, IConformanceWaiter& waiter, ContractTiming timing = {});

    // Hello, Ready with a serving HTTP listener whose backend-info names a Reboot backend, one reply
    // per request, Unsupported for unknown types, unique credentials, bind failure, stdin-EOF exit
    // and writes kept inside the work dir.
    [[nodiscard]] ConformanceReport run_backend(const ContractSubject& subject);

    // --describe, Hello, exactly the Welcome ports, ListenFailed on an occupied one, the rbsb probe,
    // Reset keeping the block, one reply per request, and Shutdown and stdin EOF within the grace.
    [[nodiscard]] ConformanceReport run_game_server(const ContractSubject& subject,
                                                    const GameServerConformanceOptions& options);

private:
    ports::IProcessLauncher& launcher_;
    IConformanceWaiter& waiter_;
    ContractTiming timing_;
};

}  // namespace reboot::testing
