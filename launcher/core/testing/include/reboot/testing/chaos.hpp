#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/testing/conformance_report.hpp"

namespace rb::testing {

class DeterministicRuntime;

struct RaceStep {
    std::string name;
    UniqueFunction<void()> run;
};

// One ordering's world, built on that ordering's own runtime.
struct RaceCase {
    std::vector<RaceStep> steps;
    // Runs once the runtime is idle, with the order the steps ran in.
    UniqueFunction<void(std::span<const std::string> order, ConformanceReport& report)> check;
};

enum class RaceMode : u8 {
    // Every step is posted before the strand runs, so they land back to back.
    Queued,
    // The strand settles after each step, so continuations interleave with the next contender.
    Settled,
};

inline constexpr std::array<RaceMode, 2> kAllRaceModes{RaceMode::Queued, RaceMode::Settled};
inline constexpr std::size_t kMaxRaceSteps = 6;

using RaceFactory = UniqueFunction<RaceCase(DeterministicRuntime& runtime)>;

// Covers no capability ids (decisions testing-strategy, async-event-model).
// Runs every ordering of the case's steps, in each mode, each on a fresh DeterministicRuntime;
// more than kMaxRaceSteps steps fails the report.
[[nodiscard]] ConformanceReport race_all_orderings(std::string_view name, RaceFactory make_case,
                                                   std::span<const RaceMode> modes = kAllRaceModes);

enum class OpContender : u8 {
    Complete,
    Fail,
    Cancel,
    // The deadline passes; it ends the op only when no AwaitingUser is pending.
    Deadline,
    // The owning connection closes; it ends only a BoundToConnection op.
    ConnectionClosed,
    // Suspends the deadline.
    AwaitingUser,
    // Re-arms a suspended deadline.
    Progress,
};

// Every ordering of `contenders` against a fresh Operation<int> of `kind` and `policy`: the first
// contender that may end the op decides its one OpCompletedEvent and its stable outcome(), and
// every later complete() returns false; when none may, nothing completes.
[[nodiscard]] ConformanceReport race_operation_outcome(OpKind kind, DisconnectPolicy policy,
                                                       std::span<const OpContender> contenders);

// `answers` valid answers to one UserRequest, optionally raced with its withdrawal: one wins by
// CAS, the others get requests.already_resolved, and one UserActionResolved is published.
[[nodiscard]] ConformanceReport race_user_request(std::size_t answers, bool with_withdrawal);

}  // namespace rb::testing
