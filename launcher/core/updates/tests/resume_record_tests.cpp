#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "reboot/storage/resume_document.hpp"
#include "reboot/updates/resume_record.hpp"

using namespace rb;
using namespace rb::updates;
using contracts::ipc::ClientKind;
using contracts::ipc::EngineOrigin;

namespace {

HostProfileId host(u8 last) {
    Uuid uuid{};
    uuid.bytes[15] = last;
    return HostProfileId{uuid};
}

LiveActivity hosted(u8 profile, SemVer payload, std::string runtime) {
    return {.kind = LiveKind::HostSession,
            .host_profile = host(profile),
            .payload_version = std::move(payload),
            .runtime_id = std::move(runtime)};
}

}  // namespace

TEST_CASE("only GUIs are reopened, once each", "[updates]") {
    ActivitySnapshot at_open;
    at_open.connected_clients = {ClientKind::WindowsGui, ClientKind::Cli, ClientKind::WindowsGui, ClientKind::Test};
    const ResumeRecord record = make_resume_record(EngineOrigin::OnDemand, at_open, std::nullopt);
    CHECK(record.reopen_clients == std::vector{ClientKind::WindowsGui});
    CHECK(record.relaunch_hosts.empty());
}

TEST_CASE("drained hosts are relaunched and keep the newest payload", "[updates]") {
    ActivitySnapshot drained;
    drained.live = {hosted(1, SemVer{1, 2, 0, ""}, "ge-proton"), hosted(2, SemVer{1, 3, 0, ""}, "ge-proton"),
                    LiveActivity{.kind = LiveKind::PlaySession, .payload_version = SemVer{1, 1, 0, ""}}};
    const ResumeRecord record = make_resume_record(EngineOrigin::ServiceManager, ActivitySnapshot{}, drained);
    CHECK(record.origin == EngineOrigin::ServiceManager);
    CHECK(record.relaunch_hosts == std::vector{host(1), host(2)});
    CHECK(record.payload_version == SemVer{1, 3, 0, ""});
    CHECK(record.runtime_ids == std::vector<std::string>{"ge-proton"});
}

TEST_CASE("a resume record survives the document", "[updates]") {
    const ResumeRecord record{.origin = EngineOrigin::ServiceManager,
                              .reopen_clients = {ClientKind::MacGui},
                              .relaunch_hosts = {host(7)},
                              .payload_version = SemVer{1, 0, 0, ""},
                              .runtime_ids = {"wine-gcenx"}};
    storage::ResumeDocument document;
    store_resume_record(record, document);
    CHECK(resume_record_from(document) == record);
}

TEST_CASE("the restart keeps the origin", "[updates]") {
    CHECK(resume_args(EngineOrigin::OnDemand) == std::vector<std::string>{"run", "--origin=on-demand", "--resume"});
    CHECK(resume_args(EngineOrigin::ServiceManager) ==
          std::vector<std::string>{"run", "--origin=service-manager", "--resume"});
    CHECK(resume_args(EngineOrigin::Foreground) == std::vector<std::string>{"run", "--foreground", "--resume"});
}
