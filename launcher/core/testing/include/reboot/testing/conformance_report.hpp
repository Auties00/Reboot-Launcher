#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/runner.hpp"

namespace reboot::testing {

// Blocked is a check that cannot run on this runtime yet, e.g. "blocked: runtime" for macOS play.
enum class CheckStatus : u8 { Passed, Failed, Skipped, Blocked };

struct ConformanceCheck {
    std::string name;
    CheckStatus status = CheckStatus::Passed;
    // The failure, or why the check was skipped or blocked.
    std::string detail;
};

// The runtime a suite ran under, so a run feeds its cell of the support matrix.
struct ConformanceRuntime {
    ports::RunnerKind kind = ports::RunnerKind::Native;
    // The pinned build, e.g. "ge-proton9-20" or "kron4ek-10.0".
    std::string id;
};

// Covers no capability ids (decision testing-strategy).
// What a conformance suite found. Suites never abort, so one run shows every deviation, and stay
// framework-free so the fake executables and OS conformance binaries can run them.
class ConformanceReport {
public:
    explicit ConformanceReport(std::string suite, std::optional<ConformanceRuntime> runtime = std::nullopt)
        : suite_(std::move(suite)), runtime_(std::move(runtime)) {}

    // Records `condition` under `check` and returns it, so a suite can skip dependent checks.
    bool expect(std::string_view check, bool condition, std::string_view detail = {});
    // Passes when `result` succeeded; a failure records its diagnostic.
    template <class T>
    bool expect_ok(std::string_view check, const Result<T>& result) {
        return record_ok(check, result ? nullptr : &result.error());
    }
    // Passes when `result` failed with `kind`.
    template <class T>
    bool expect_error(std::string_view check, const Result<T>& result, ErrorKind kind) {
        return record_error(check, result ? nullptr : &result.error(), kind);
    }
    void skip(std::string_view check, std::string reason);
    void block(std::string_view check, std::string reason);
    void merge(const ConformanceReport& other);

    // Skipped and blocked checks do not fail a report.
    [[nodiscard]] bool passed() const noexcept;
    [[nodiscard]] const std::string& suite() const noexcept { return suite_; }
    [[nodiscard]] const std::optional<ConformanceRuntime>& runtime() const noexcept { return runtime_; }
    [[nodiscard]] std::span<const ConformanceCheck> checks() const noexcept { return checks_; }
    // One line per failed check, then the blocked and skipped ones; empty when everything passed.
    [[nodiscard]] std::string describe() const;

private:
    bool record_ok(std::string_view check, const Diagnostic* error);
    bool record_error(std::string_view check, const Diagnostic* error, ErrorKind kind);

    std::string suite_;
    std::optional<ConformanceRuntime> runtime_;
    std::vector<ConformanceCheck> checks_;
};

// "<id>(name=value, ...)": how reports and chaos helpers show a Diagnostic.
[[nodiscard]] std::string describe_diagnostic(const Diagnostic& diag);

}  // namespace reboot::testing
