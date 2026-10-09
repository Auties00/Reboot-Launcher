#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "bundle_layout.hpp"
#include "dns_answers.hpp"
#include "integration_rules.hpp"

using namespace rb::os_macos::platform;
using rb::IpAddress;
using rb::NativePath;
using rb::ports::IntegrationKind;
using rb::ports::IntegrationState;

TEST_CASE("the app bundle is found from Contents/MacOS only", "[bundle_layout]") {
    CHECK(app_bundle_of("/Applications/Reboot Launcher.app/Contents/MacOS") == NativePath{"/Applications/Reboot Launcher.app"});
    CHECK(app_bundle_of("/Applications/Reboot Launcher.app/Contents/MacOS/") == NativePath{"/Applications/Reboot Launcher.app"});
    CHECK_FALSE(app_bundle_of("/Users/dev/build/core/engine"));
    CHECK_FALSE(app_bundle_of("/Applications/Reboot Launcher.app/Contents/Frameworks"));
    CHECK_FALSE(app_bundle_of("/Applications/Thing/Contents/MacOS"));
    CHECK_FALSE(app_bundle_of("/.app/Contents/MacOS"));
}

TEST_CASE("the reboot scheme handler is classified by bundle path and identifier", "[integration_rules]") {
    const NativePath ours{"/Applications/Reboot Launcher.app"};
    const std::optional<std::string> id{"dev.projectreboot.launcher"};
    CHECK(scheme_state(ours, id, std::nullopt) == IntegrationState::Absent);
    CHECK(scheme_state(ours, id, SchemeHandler{NativePath{"/Applications/Reboot Launcher.app/"}, id}) == IntegrationState::Ours);
    CHECK(scheme_state(ours, id, SchemeHandler{NativePath{"/Users/u/Downloads/Reboot Launcher.app"}, id}) ==
          IntegrationState::Stale);
    CHECK(scheme_state(ours, id, SchemeHandler{NativePath{"/Applications/Other.app"}, "com.example.other"}) ==
          IntegrationState::Foreign);
    CHECK(scheme_state(ours, std::nullopt, SchemeHandler{NativePath{"/Applications/Other.app"}, std::nullopt}) ==
          IntegrationState::Foreign);
}

TEST_CASE("agent states map onto integration states", "[integration_rules]") {
    const auto enabled = agent_integration_status(IntegrationKind::Autostart, shims::AgentStatus::Enabled);
    CHECK(enabled.kind == IntegrationKind::Autostart);
    CHECK(enabled.state == IntegrationState::Ours);
    CHECK(enabled.detail.empty());
    const auto approval = agent_integration_status(IntegrationKind::EngineAgent, shims::AgentStatus::RequiresApproval);
    CHECK(approval.state == IntegrationState::Ours);
    CHECK(approval.detail == "requires_approval");
    CHECK(agent_integration_status(IntegrationKind::EngineAgent, shims::AgentStatus::NotRegistered).state ==
          IntegrationState::Absent);
    CHECK(agent_integration_status(IntegrationKind::EngineAgent, shims::AgentStatus::NotFound).state ==
          IntegrationState::Absent);
}

TEST_CASE("only our own agents' launchd labels are kept", "[integration_rules]") {
    CHECK(own_agent_label("dev.projectreboot.launcher.engine") == "dev.projectreboot.launcher.engine");
    CHECK(own_agent_label("dev.projectreboot.launcher.engine-login") == "dev.projectreboot.launcher.engine-login");
    CHECK_FALSE(own_agent_label("application.dev.projectreboot.launcher.1234.5678"));
    CHECK_FALSE(own_agent_label("0"));
    CHECK_FALSE(own_agent_label(""));
    CHECK_FALSE(own_agent_label(std::nullopt));
}

TEST_CASE("a lookup completes once both families answered", "[dns_answers]") {
    DnsAnswers answers;
    answers.add(IpAddress::v4(0x7F000001));
    CHECK_FALSE(answers.complete());
    answers.add(IpAddress::v4(0x7F000001));
    answers.none(DnsFamily::V6);
    CHECK(answers.complete());
    CHECK(answers.addresses().size() == 1);

    DnsAnswers v6;
    v6.add(*IpAddress::parse("::1"));
    CHECK_FALSE(v6.complete());
    v6.none(DnsFamily::V4);
    CHECK(v6.complete());
    CHECK(v6.addresses().front().is_loopback());
}

TEST_CASE("special-use names never reach DNS", "[dns_answers]") {
    CHECK(special_name("localhost") == SpecialName::Loopback);
    CHECK(special_name("LOCALHOST.") == SpecialName::Loopback);
    CHECK(special_name("app.localhost") == SpecialName::Loopback);
    CHECK(special_name("reboot-conformance.invalid") == SpecialName::Invalid);
    CHECK(special_name("invalid") == SpecialName::Invalid);
    CHECK(special_name("notlocalhost") == SpecialName::None);
    CHECK(special_name("invalid.example.com") == SpecialName::None);
    CHECK(special_name("projectreboot.dev") == SpecialName::None);
    CHECK(special_name("") == SpecialName::None);
}
