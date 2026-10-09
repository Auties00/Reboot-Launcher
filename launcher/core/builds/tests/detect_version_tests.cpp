#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <variant>

#include "builds_test_support.hpp"
#include "reboot/builds/cl_table.hpp"
#include "reboot/builds/detect_version.hpp"
#include "reboot/builds/pe_version_reader.hpp"
#include "reboot/catalog/alias_table.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/testing/golden.hpp"

using namespace rb;
using namespace rb::builds;
using namespace rb::builds::test;

namespace {

std::vector<u8> golden_blob(std::string_view name) {
    auto bytes = testing::read_golden(NativePath(REBOOT_BUILDS_TEST_DATA) / NativePath(name));
    REQUIRE(bytes);
    return *bytes;
}

const NativePath kCrashReport = NativePath("Engine") / "Binaries" / "Win64" / std::string(kCrashReportClientExe);
const NativePath kShipping = NativePath("FortniteGame") / "Binaries" / "Win64" / std::string(kShippingExe);

struct Fixture {
    testing::ScratchDir dir = make_scratch("reboot-builds-detect");
    ClTable cl_table;
    DetectionTables tables{.cl_table = cl_table, .aliases = catalog::AliasTable{}};
    PeVersionReader reader;

    BuildLayout layout(bool with_crash_report) const {
        BuildLayout out{.root = dir.path(), .shipping_exe = kShipping};
        if (with_crash_report) out.crash_report_clients.push_back(kCrashReport);
        return out;
    }

    VersionDetection detect(const BuildLayout& build, CancelToken token = {}) const {
        Result<VersionDetection> detection = detect_version(build, reader, tables, token);
        REQUIRE(detection);
        return std::move(*detection);
    }
};

}  // namespace

TEST_CASE("CrashReportClient settles a Cert build through the CL table") {
    Fixture f;
    write_file(f.dir.path() / kCrashReport, make_pe(golden_blob("version_cert.bin")));
    write_file(f.dir.path() / kShipping, make_pe(version_blob("++Fortnite+Release-9.99")));

    const VersionDetection detection = f.detect(f.layout(true));
    const auto* detected = std::get_if<DetectedVersion>(&detection);
    REQUIRE(detected);
    CHECK(detected->version == version("1.7.2"));
    CHECK(detected->source == VersionSource::ClTable);
    CHECK(detected->file == kCrashReport);
}

TEST_CASE("the shipping exe's resource is read when CrashReportClient names nothing") {
    Fixture f;
    write_file(f.dir.path() / kCrashReport, make_pe(version_blob("CompanyName Epic Games")));
    write_file(f.dir.path() / kShipping, make_pe(golden_blob("version_release_34_10_cl.bin")));

    const VersionDetection detection = f.detect(f.layout(true));
    const auto* detected = std::get_if<DetectedVersion>(&detection);
    REQUIRE(detected);
    CHECK(detected->version == version("34.10"));
    CHECK(detected->cl == Changelist{40567068});
    CHECK(detected->source == VersionSource::PeResource);
    CHECK(detected->file == kShipping);
}

TEST_CASE("the raw scan is the last resort") {
    Fixture f;
    const std::vector<u8> trailer = utf16le("4.20.0-4008490+++Fortnite+Release-3.5");
    write_file(f.dir.path() / kShipping, make_pe(version_blob("no release here"), trailer));

    const VersionDetection detection = f.detect(f.layout(false));
    const auto* detected = std::get_if<DetectedVersion>(&detection);
    REQUIRE(detected);
    CHECK(detected->version == version("3.5"));
    CHECK(detected->source == VersionSource::RawScan);
}

TEST_CASE("files that settle nothing ask the user, with every reason and the raw tail") {
    Fixture f;
    write_text(f.dir.path() / kCrashReport, "not an executable");
    write_file(f.dir.path() / kShipping, make_pe(version_blob("++Fortnite+Release-Main")));

    const VersionDetection detection = f.detect(f.layout(true));
    const auto* needs = std::get_if<NeedsUserVersion>(&detection);
    REQUIRE(needs);
    CHECK(needs->raw == "Main");
    REQUIRE(needs->reasons.size() == 3);
    CHECK(needs->reasons[0].id == "builds.version_file_unreadable");
    CHECK(needs->reasons[0].causes.at(0).id == "builds.pe_not_pe");
    CHECK(needs->reasons[1].id == "builds.unknown_release_shape");
    CHECK(arg_text(needs->reasons[1], "raw") == "Main");
    CHECK(needs->reasons[2].id == "builds.unknown_release_shape");
}

TEST_CASE("a Cert build of an unknown changelist names that changelist") {
    Fixture f;
    write_file(f.dir.path() / kShipping, make_pe(version_blob("4.16.0-3700115+++Fortnite+Release-Cert")));

    const VersionDetection detection = f.detect(f.layout(false));
    const auto* needs = std::get_if<NeedsUserVersion>(&detection);
    REQUIRE(needs);
    REQUIRE_FALSE(needs->reasons.empty());
    CHECK(needs->reasons[0].id == "builds.unknown_changelist");
    CHECK(arg_text(needs->reasons[0], "cl") == "3700115");
}

TEST_CASE("a missing file is a reason, and a cancel ends detection") {
    Fixture f;
    const VersionDetection detection = f.detect(f.layout(true));
    const auto* needs = std::get_if<NeedsUserVersion>(&detection);
    REQUIRE(needs);
    CHECK(needs->reasons.size() == 3);
    CHECK(needs->reasons[0].id == "builds.version_file_unreadable");

    CancelSource cancel;
    cancel.cancel(CancelReason::User);
    const auto cancelled = detect_version(f.layout(true), f.reader, f.tables, cancel.token());
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().id == "builds.cancelled");
}
