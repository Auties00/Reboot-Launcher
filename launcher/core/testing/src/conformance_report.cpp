#include "reboot/testing/conformance_report.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/version.hpp"

namespace reboot::testing {
namespace {

[[nodiscard]] std::string describe_arg(const Arg& arg) {
    return std::visit(
        []<class V>(const V& value) -> std::string {
            if constexpr (std::is_same_v<V, std::string>) {
                return value;
            } else if constexpr (std::is_same_v<V, bool>) {
                return value ? "true" : "false";
            } else if constexpr (std::is_same_v<V, std::chrono::milliseconds>) {
                return std::format("{}ms", value.count());
            } else if constexpr (std::is_same_v<V, WirePath>) {
                return value.display;
            } else if constexpr (std::is_same_v<V, SemVer>) {
                return value.to_string();
            } else {
                return std::to_string(value);
            }
        },
        arg);
}

[[nodiscard]] std::string_view status_name(CheckStatus status) noexcept {
    switch (status) {
        case CheckStatus::Passed: return "passed";
        case CheckStatus::Failed: return "FAILED";
        case CheckStatus::Skipped: return "skipped";
        case CheckStatus::Blocked: return "blocked";
    }
    return "unknown";
}

}  // namespace

bool ConformanceReport::expect(std::string_view check, bool condition, std::string_view detail) {
    checks_.push_back({std::string(check), condition ? CheckStatus::Passed : CheckStatus::Failed,
                       condition ? std::string() : std::string(detail)});
    return condition;
}

bool ConformanceReport::record_ok(std::string_view check, const Diagnostic* error) {
    return expect(check, error == nullptr, error == nullptr ? std::string() : describe_diagnostic(*error));
}

bool ConformanceReport::record_error(std::string_view check, const Diagnostic* error, ErrorKind kind) {
    if (error == nullptr) return expect(check, false, "succeeded instead of failing");
    return expect(check, error->kind == kind,
                  std::format("wrong ErrorKind {} in {}", static_cast<int>(error->kind), describe_diagnostic(*error)));
}

void ConformanceReport::skip(std::string_view check, std::string reason) {
    checks_.push_back({std::string(check), CheckStatus::Skipped, std::move(reason)});
}

void ConformanceReport::block(std::string_view check, std::string reason) {
    checks_.push_back({std::string(check), CheckStatus::Blocked, std::move(reason)});
}

void ConformanceReport::merge(const ConformanceReport& other) {
    for (const ConformanceCheck& check : other.checks_)
        checks_.push_back({other.suite_ + "/" + check.name, check.status, check.detail});
}

bool ConformanceReport::passed() const noexcept {
    return std::ranges::none_of(checks_, [](const ConformanceCheck& check) { return check.status == CheckStatus::Failed; });
}

std::string ConformanceReport::describe() const {
    std::string out;
    for (const CheckStatus status : {CheckStatus::Failed, CheckStatus::Blocked, CheckStatus::Skipped}) {
        for (const ConformanceCheck& check : checks_) {
            if (check.status != status) continue;
            out += std::format("{} {}/{}: {}\n", status_name(status), suite_, check.name, check.detail);
        }
    }
    return out;
}

std::string describe_diagnostic(const Diagnostic& diag) {
    std::string out = diag.id + "(";
    for (std::size_t i = 0; i < diag.args.size(); ++i) {
        if (i > 0) out += ", ";
        out += diag.args[i].first + "=" + describe_arg(diag.args[i].second);
    }
    return out + ")";
}

}  // namespace reboot::testing
