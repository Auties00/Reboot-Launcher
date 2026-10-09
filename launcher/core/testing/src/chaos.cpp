#include "reboot/testing/chaos.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <chrono>
#include <cstddef>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/event_recorder.hpp"

namespace reboot::testing {
namespace {

constexpr MessageId kRequestAlreadyResolved{"requests.already_resolved"};
constexpr ConnectionId kRaceConnection{1};
constexpr int kCompletedValue = 7;

[[nodiscard]] std::string_view mode_name(RaceMode mode) {
    return mode == RaceMode::Queued ? "queued" : "settled";
}

[[nodiscard]] std::string join(std::span<const std::string> names) {
    std::string out;
    for (const std::string& name : names) {
        if (!out.empty()) out += ",";
        out += name;
    }
    return out;
}

[[nodiscard]] std::string_view contender_name(OpContender contender) {
    switch (contender) {
        case OpContender::Complete: return "complete";
        case OpContender::Fail: return "fail";
        case OpContender::Cancel: return "cancel";
        case OpContender::Deadline: return "deadline";
        case OpContender::ConnectionClosed: return "connection_closed";
        case OpContender::AwaitingUser: return "awaiting_user";
        case OpContender::Progress: return "progress";
    }
    return "unknown";
}

// The outcome an ordering must produce, replayed from OpRegistry's rules.
struct ExpectedOp {
    enum class End : u8 { None, Completed, Failed, CancelledUser, CancelledDisconnect, TimedOut };

    End end = End::None;
    // Which step ended it, for the complete() return values.
    std::optional<std::size_t> ended_by;
    bool awaiting = false;
    // The armed deadline has passed and its timer has not run yet.
    bool deadline_due = false;
    // AwaitingUser came after the deadline passed: the budget left to re-arm with is zero.
    bool budget_spent = false;
    bool attached = true;
    // The connection let go of a finished op, so the registry dropped its outcome.
    bool released = false;
};

[[nodiscard]] std::string_view end_name(ExpectedOp::End end) {
    switch (end) {
        case ExpectedOp::End::None: return "none";
        case ExpectedOp::End::Completed: return "completed";
        case ExpectedOp::End::Failed: return "failed";
        case ExpectedOp::End::CancelledUser: return "cancelled(user)";
        case ExpectedOp::End::CancelledDisconnect: return "cancelled(disconnect)";
        case ExpectedOp::End::TimedOut: return "timed_out";
    }
    return "unknown";
}

[[nodiscard]] ExpectedOp::End actual_end(const ErasedOutcome& outcome) {
    if (std::holds_alternative<Completed<std::any>>(outcome)) return ExpectedOp::End::Completed;
    if (std::holds_alternative<Failed>(outcome)) return ExpectedOp::End::Failed;
    if (const auto* cancelled = std::get_if<Cancelled>(&outcome))
        return cancelled->reason == CancelReason::Disconnect ? ExpectedOp::End::CancelledDisconnect
                                                             : ExpectedOp::End::CancelledUser;
    return ExpectedOp::End::TimedOut;
}

// A connection letting go of a finished op drops it, Operation included.
[[nodiscard]] bool held(DeterministicRuntime& runtime, OpId id) {
    if (runtime.ops().outcome(id)) return true;
    return std::ranges::any_of(runtime.ops().live(), [id](const LiveOp& op) { return op.op == id; });
}

void fire_deadline(ExpectedOp& expected) {
    if (!expected.deadline_due) return;
    expected.deadline_due = false;
    if (expected.end == ExpectedOp::End::None && !expected.awaiting) expected.end = ExpectedOp::End::TimedOut;
}

[[nodiscard]] ExpectedOp replay(std::span<const OpContender> contenders, std::span<const std::size_t> order,
                                OpKind kind, DisconnectPolicy policy, RaceMode mode) {
    ExpectedOp expected;
    for (const std::size_t index : order) {
        const bool open = expected.end == ExpectedOp::End::None;
        const auto end_with = [&](ExpectedOp::End end) {
            if (!open) return;
            expected.end = end;
            expected.ended_by = index;
            expected.deadline_due = false;
        };
        switch (contenders[index]) {
            case OpContender::Complete: end_with(ExpectedOp::End::Completed); break;
            case OpContender::Fail: end_with(ExpectedOp::End::Failed); break;
            case OpContender::Cancel: end_with(ExpectedOp::End::CancelledUser); break;
            case OpContender::ConnectionClosed:
                if (!expected.attached) break;
                if (policy == DisconnectPolicy::BoundToConnection) end_with(ExpectedOp::End::CancelledDisconnect);
                expected.attached = false;
                expected.released = expected.end != ExpectedOp::End::None;
                break;
            case OpContender::AwaitingUser:
                if (open && !expected.awaiting) {
                    expected.awaiting = true;
                    expected.budget_spent = expected.deadline_due;
                    expected.deadline_due = false;
                }
                break;
            case OpContender::Progress:
                if (!open) break;
                if (uses_liveness_deadline(kind)) {
                    expected.deadline_due = false;
                } else if (expected.awaiting) {
                    // Re-armed with what was left, which may be nothing.
                    expected.deadline_due = expected.budget_spent;
                }
                expected.awaiting = false;
                expected.budget_spent = false;
                break;
            case OpContender::Deadline:
                if (open && !expected.awaiting) expected.deadline_due = true;
                break;
        }
        if (mode == RaceMode::Settled) fire_deadline(expected);
    }
    // Queued: the deadline timer lands behind every queued step.
    fire_deadline(expected);
    return expected;
}

}  // namespace

ConformanceReport race_all_orderings(std::string_view name, RaceFactory make_case, std::span<const RaceMode> modes) {
    ConformanceReport report{std::string(name)};
    std::size_t step_count = 0;
    {
        DeterministicRuntime probe;
        step_count = make_case(probe).steps.size();
    }
    if (!report.expect("at most kMaxRaceSteps steps", step_count <= kMaxRaceSteps,
                       std::to_string(step_count) + " steps make too many orderings"))
        return report;

    for (const RaceMode mode : modes) {
        std::vector<std::size_t> order(step_count);
        std::iota(order.begin(), order.end(), std::size_t{0});
        do {
            DeterministicRuntime runtime;
            RaceCase race = make_case(runtime);
            if (!report.expect("every ordering builds the same steps", race.steps.size() == step_count)) return report;
            std::vector<std::string> names;
            names.reserve(step_count);
            for (const std::size_t index : order) {
                RaceStep& step = race.steps[index];
                names.push_back(step.name);
                runtime.strand().post(std::move(step.run));
                if (mode == RaceMode::Settled) runtime.run_until_idle();
            }
            runtime.run_until_idle();
            ConformanceReport ordering(std::string(mode_name(mode)) + "[" + join(names) + "]");
            if (race.check) race.check(names, ordering);
            report.merge(ordering);
        } while (std::ranges::next_permutation(order).found);
    }
    return report;
}

ConformanceReport race_operation_outcome(OpKind kind, DisconnectPolicy policy, std::span<const OpContender> contenders) {
    const std::vector<OpContender> roster(contenders.begin(), contenders.end());
    ConformanceReport report("operation_outcome");
    for (const RaceMode mode : kAllRaceModes) {
        const std::array<RaceMode, 1> one{mode};
        report.merge(race_all_orderings(
            mode_name(mode),
            [&roster, kind, policy, mode](DeterministicRuntime& runtime) {
                auto [handle, op] = runtime.ops().create<int>(kind, policy, std::nullopt);
                const OpId id = handle.id();
                (void)runtime.ops().attach(id, kRaceConnection);
                auto recorder = std::make_shared<EventRecorder>(runtime.events(), EventFilter{{EventKind::OpCompleted}, {}, id});
                auto returned = std::make_shared<std::vector<std::optional<bool>>>(roster.size());
                const auto deadline = default_deadline(kind) + std::chrono::seconds{1};

                RaceCase race;
                for (std::size_t i = 0; i < roster.size(); ++i) {
                    UniqueFunction<void()> run;
                    switch (roster[i]) {
                        case OpContender::Complete:
                            run = [&runtime, &op, returned, id, i] {
                                if (held(runtime, id)) (*returned)[i] = op.complete(Completed<int>{kCompletedValue});
                            };
                            break;
                        case OpContender::Fail:
                            run = [&runtime, &op, returned, id, i] {
                                if (held(runtime, id))
                                    (*returned)[i] =
                                        op.complete(Failed{make_diag(ErrorDomain::Internal, MessageId{"internal.bug"})});
                            };
                            break;
                        case OpContender::Cancel:
                            run = [&runtime, id] { (void)runtime.ops().cancel(id, CancelReason::User); };
                            break;
                        case OpContender::Deadline:
                            run = [&runtime, deadline] { runtime.clock().advance(deadline); };
                            break;
                        case OpContender::ConnectionClosed:
                            run = [&runtime] { runtime.ops().on_connection_closed(kRaceConnection); };
                            break;
                        case OpContender::AwaitingUser:
                            run = [&runtime, &op, id] {
                                if (held(runtime, id)) op.awaiting_user(RequestId{1});
                            };
                            break;
                        case OpContender::Progress:
                            run = [&runtime, &op, id] {
                                if (held(runtime, id)) op.progress(Progress{"racing", 1, std::nullopt, std::nullopt, std::nullopt});
                            };
                            break;
                    }
                    race.steps.push_back({std::string(contender_name(roster[i])) + "#" + std::to_string(i), std::move(run)});
                }
                race.check = [&runtime, &roster, recorder, returned, id, kind, policy, mode](
                                 std::span<const std::string> order_names, ConformanceReport& check) {
                    std::vector<std::size_t> order;
                    for (const std::string& step : order_names)
                        order.push_back(static_cast<std::size_t>(std::stoul(step.substr(step.find('#') + 1))));
                    const ExpectedOp expected = replay(roster, order, kind, policy, mode);
                    recorder->pump();
                    const std::size_t completions = recorder->count(EventKind::OpCompleted);
                    const std::optional<ErasedOutcome> outcome = runtime.ops().outcome(id);
                    const auto events = recorder->payloads<OpCompletedEvent>(EventKind::OpCompleted);
                    if (expected.end == ExpectedOp::End::None) {
                        check.expect("nothing completes", completions == 0 && !outcome,
                                     std::to_string(completions) + " completions");
                    } else {
                        check.expect("exactly one OpCompletedEvent", completions == 1, std::to_string(completions));
                        const ExpectedOp::End got = events.empty() ? ExpectedOp::End::None : actual_end(events.front()->outcome);
                        check.expect("the first contender that may end it decides the outcome", got == expected.end,
                                     "expected " + std::string(end_name(expected.end)) + ", got " + std::string(end_name(got)));
                        if (expected.released) {
                            check.expect("a released outcome is dropped", !outcome);
                        } else {
                            check.expect("outcome() keeps what the event carried", outcome && actual_end(*outcome) == got);
                        }
                        if (got == ExpectedOp::End::Completed && outcome) {
                            const auto* value = std::any_cast<int>(&std::get<Completed<std::any>>(*outcome).value);
                            check.expect("the completed value is kept", value != nullptr && *value == kCompletedValue);
                        }
                    }
                    for (std::size_t i = 0; i < roster.size(); ++i) {
                        if (!(*returned)[i]) continue;
                        const bool should = expected.ended_by == i;
                        check.expect("complete() returns true only for the winner (" + std::to_string(i) + ")",
                                     *(*returned)[i] == should);
                    }
                };
                return race;
            },
            one));
    }
    return report;
}

ConformanceReport race_user_request(std::size_t answers, bool with_withdrawal) {
    return race_all_orderings("user_request", [answers, with_withdrawal](DeterministicRuntime& runtime) {
        auto source = std::make_shared<CancelSource>();
        auto recorder = std::make_shared<EventRecorder>(runtime.events(), EventFilter{{EventKind::UserActionResolved}, {}, {}});
        auto results = std::make_shared<std::vector<std::optional<Result<void>>>>(answers);
        const RequestId id = runtime.requests().ask(
            UserRequestKind::ConfirmJoin, std::any{}, std::nullopt, std::nullopt,
            [](const std::any&) -> Result<void> { return {}; }, source->token());

        RaceCase race;
        for (std::size_t i = 0; i < answers; ++i)
            race.steps.push_back({"answer#" + std::to_string(i), [&runtime, results, id, i] {
                                      (*results)[i] = runtime.requests().respond(id, std::any(static_cast<int>(i)));
                                  }});
        if (with_withdrawal) race.steps.push_back({"withdraw", [source] { source->cancel(CancelReason::User); }});

        race.check = [&runtime, recorder, results, source](std::span<const std::string> order, ConformanceReport& check) {
            const bool withdrawn_first = !order.empty() && order.front() == "withdraw";
            std::size_t accepted = 0;
            bool others_already_resolved = true;
            for (const auto& result : *results) {
                if (!result) continue;
                if (*result) {
                    ++accepted;
                } else if (!result->error().is(kRequestAlreadyResolved)) {
                    others_already_resolved = false;
                }
            }
            check.expect("one answer wins by CAS", accepted == (withdrawn_first || results->empty() ? 0u : 1u),
                         std::to_string(accepted) + " answers accepted");
            check.expect("the others get requests.already_resolved", others_already_resolved);
            recorder->pump();
            const auto resolved = recorder->payloads<UserActionResolvedEvent>(EventKind::UserActionResolved);
            const bool expect_event = !results->empty() || withdrawn_first;
            check.expect("one UserActionResolved is published", resolved.size() == (expect_event ? 1u : 0u),
                         std::to_string(resolved.size()) + " events");
            if (resolved.size() == 1)
                check.expect("it says how the request ended",
                             resolved.front()->resolution ==
                                 (withdrawn_first ? RequestResolution::Withdrawn : RequestResolution::Answered));
            check.expect("nothing stays pending", runtime.requests().pending().empty() || !expect_event);
        };
        return race;
    });
}

}  // namespace reboot::testing
