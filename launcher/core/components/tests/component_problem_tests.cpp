#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "components_test_support.hpp"
#include "reboot/components/bundled_assets.hpp"
#include "reboot/components/component_problem.hpp"
#include "reboot/testing/in_memory_file_system.hpp"

using namespace rb;
using namespace rb::components;
using rb::components::test::arg_text;

namespace {

ComponentProblem problem(ComponentProblemKind kind, std::optional<ports::SecurityProducts> security) {
    return ComponentProblem{.kind = kind,
                            .component = ComponentRef{ComponentKind::Payload, "payload", "1.0.0"},
                            .file = NativePath("/data/components/payload/ab/rb_client.dll"),
                            .security = std::move(security)};
}

}  // namespace

TEST_CASE("without a security center there is nothing to guide", "[components][problem]") {
    CHECK(guided_remediation(std::nullopt).empty());
}

TEST_CASE("remediation names the restore path, and reports only a named product", "[components][problem]") {
    using enum RemediationStep;
    const ports::SecurityProducts unnamed{{}, ports::SmartAppControl::Off};
    CHECK(guided_remediation(unnamed) == std::vector{RestoreFromQuarantine, OpenSecurityCenter, CopyRestoreCommand});

    const ports::SecurityProducts defender{{"Microsoft Defender Antivirus"}, ports::SmartAppControl::Unknown};
    CHECK(guided_remediation(defender) ==
          std::vector{RestoreFromQuarantine, OpenSecurityCenter, CopyRestoreCommand, ReportFalsePositive});

    for (const auto state : {ports::SmartAppControl::On, ports::SmartAppControl::Evaluation}) {
        const ports::SecurityProducts sac{{"Microsoft Defender Antivirus"}, state};
        CHECK(guided_remediation(sac).back() == ExplainSmartAppControl);
    }
}

TEST_CASE("a problem names security software only when the probe listed a product", "[components][problem]") {
    const ports::SecurityProducts products{{"Defender", "Other AV"}, ports::SmartAppControl::On};
    const Diagnostic attributed = to_diagnostic(problem(ComponentProblemKind::FileVanishedAfterVerify, products));
    CHECK(attributed.id == "components.file_quarantined");
    CHECK(arg_text(attributed, "security_products") == "Defender, Other AV");
    CHECK(arg_text(attributed, "smart_app_control") == "on");
    CHECK(arg_text(attributed, "file").ends_with("rb_client.dll"));

    CHECK(to_diagnostic(problem(ComponentProblemKind::FileVanishedAfterVerify, std::nullopt)).id ==
          "components.file_vanished");
    // An empty list attributes nothing.
    const Diagnostic unnamed =
        to_diagnostic(problem(ComponentProblemKind::FileVanishedAfterVerify, ports::SecurityProducts{}));
    CHECK(unnamed.id == "components.file_vanished");
    CHECK(unnamed.find_arg("security_products") == nullptr);

    CHECK(to_diagnostic(problem(ComponentProblemKind::VanishedAfterDownload, products)).id ==
          "components.download_quarantined");
    CHECK(to_diagnostic(problem(ComponentProblemKind::VanishedAfterDownload, std::nullopt)).id ==
          "components.download_vanished");
    CHECK(to_diagnostic(problem(ComponentProblemKind::HelperQuarantined, products)).id ==
          "components.helper_quarantined");
    CHECK(to_diagnostic(problem(ComponentProblemKind::HelperQuarantined, std::nullopt)).id ==
          "components.helper_vanished");
}

TEST_CASE("a failed re-fetch rides along as the cause", "[components][problem]") {
    ComponentProblem failed = problem(ComponentProblemKind::FileVanishedAfterVerify, std::nullopt);
    failed.recovery = RecoveryState::RefetchFailed;
    failed.refetch_error = make_diag(ErrorDomain::Components, MessageId{"components.download_failed"}).build();
    const Diagnostic diag = to_diagnostic(failed);
    REQUIRE(diag.causes.size() == 1);
    CHECK(diag.causes[0].id == "components.download_failed");
}

TEST_CASE("find_missing_assets lists what the install lacks", "[components][assets]") {
    testing::InMemoryFileSystem fs;
    InstallLayout install{.install_dir = "/app",
                          .backend_exe = "/app/reboot-backend",
                          .game_server_exe = "/app/reboot-game-server",
                          .backend_content_dir = "/app/backend-content",
                          .bundled_catalog = "/app/catalog.json",
                          .bundled_manifest = "/app/manifest.json"};
    CHECK(bundled_asset_path(install, BundledAsset::BundledManifestSignature) == NativePath("/app/manifest.json.sig"));

    fs.write_text(install.backend_exe, "exe");
    fs.write_text(install.game_server_exe, "exe");
    fs.make_dir(install.backend_content_dir);
    fs.write_text(install.bundled_catalog, "{}");
    fs.write_text(install.bundled_manifest, "{}");
    auto missing = find_missing_assets(fs, install);
    REQUIRE(missing);
    REQUIRE(missing->size() == 1);
    CHECK((*missing)[0].asset == BundledAsset::BundledManifestSignature);
    const Diagnostic diag = to_diagnostic((*missing)[0]);
    CHECK(diag.id == "components.bundled_asset_missing");
    CHECK(arg_text(diag, "path").ends_with("manifest.json.sig"));

    fs.write_text("/app/manifest.json.sig", "sig");
    missing = find_missing_assets(fs, install);
    REQUIRE(missing);
    CHECK(missing->empty());

    fs.faults().fail_next(testing::FsOperation::Revision,
                          make_diag(ErrorDomain::Platform, MessageId{"components_test.denied"}).build());
    CHECK_FALSE(find_missing_assets(fs, install));
}
