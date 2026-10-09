#include "reboot/components/component_problem.hpp"

#include <string>
#include <string_view>

#include "messages.hpp"

namespace reboot::components {

namespace {

[[nodiscard]] std::string_view smart_app_control_name(ports::SmartAppControl state) noexcept {
    switch (state) {
        case ports::SmartAppControl::Off: return "off";
        case ports::SmartAppControl::Evaluation: return "evaluation";
        case ports::SmartAppControl::On: return "on";
        case ports::SmartAppControl::Unknown: return "unknown";
    }
    return "unknown";
}

[[nodiscard]] std::string join_names(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        if (!out.empty()) out += ", ";
        out += name;
    }
    return out;
}

}  // namespace

std::vector<RemediationStep> guided_remediation(const std::optional<ports::SecurityProducts>& security) {
    // Without a security center there is no quarantine to restore from.
    if (!security) return {};
    std::vector<RemediationStep> steps{RemediationStep::RestoreFromQuarantine, RemediationStep::OpenSecurityCenter,
                                       RemediationStep::CopyRestoreCommand};
    if (!security->names.empty()) steps.push_back(RemediationStep::ReportFalsePositive);
    if (security->smart_app_control == ports::SmartAppControl::On ||
        security->smart_app_control == ports::SmartAppControl::Evaluation)
        steps.push_back(RemediationStep::ExplainSmartAppControl);
    return steps;
}

Diagnostic to_diagnostic(const ComponentProblem& problem) {
    const bool attributed = problem.security && !problem.security->names.empty();
    MessageId message = kFileVanished;
    switch (problem.kind) {
        case ComponentProblemKind::VanishedAfterDownload:
            message = attributed ? kDownloadQuarantined : kDownloadVanished;
            break;
        case ComponentProblemKind::FileVanishedAfterVerify:
            message = attributed ? kFileQuarantined : kFileVanished;
            break;
        case ComponentProblemKind::HelperQuarantined:
            message = attributed ? kHelperQuarantined : kHelperVanished;
            break;
    }
    DiagBuilder builder = make_diag(ErrorDomain::Components, message).arg("file", problem.file);
    // The builder's setters mutate it in place.
    if (attributed)
        std::move(builder)
            .arg("security_products", join_names(problem.security->names))
            .arg("smart_app_control", smart_app_control_name(problem.security->smart_app_control));
    if (problem.refetch_error) std::move(builder).cause(*problem.refetch_error);
    return std::move(builder).build();
}

}  // namespace reboot::components
