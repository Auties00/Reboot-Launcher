#include <catch2/catch_test_macros.hpp>

#include "reboot/host/host_phase.hpp"

using namespace rb;
using namespace rb::host;

using sessions::SessionPhase;

TEST_CASE("host phases report their registry phase", "[host]") {
    CHECK(session_phase_for(HostPhase::Preparing) == SessionPhase::Preparing);
    CHECK(session_phase_for(HostPhase::Spawning) == SessionPhase::Launching);
    CHECK(session_phase_for(HostPhase::WaitingForListen) == SessionPhase::Launching);
    CHECK(session_phase_for(HostPhase::WaitingForReadiness) == SessionPhase::Loading);
    CHECK(session_phase_for(HostPhase::MappingPorts) == SessionPhase::Loading);
    CHECK(session_phase_for(HostPhase::Publishing) == SessionPhase::Loading);
}

// A restart or drain must not look like a new session to the registry.
TEST_CASE("every phase of a live server is Running", "[host]") {
    for (const HostPhase phase : {HostPhase::Live, HostPhase::LiveUnpublished, HostPhase::Restarting,
                                  HostPhase::Draining})
        CHECK(session_phase_for(phase) == SessionPhase::Running);
}

TEST_CASE("only the registry's stop path enters the ending phases", "[host]") {
    for (const HostPhase phase : {HostPhase::Stopping, HostPhase::Stopped, HostPhase::Failed})
        CHECK_FALSE(session_phase_for(phase));
}

TEST_CASE("every host phase has a name", "[host]") {
    for (u8 i = 0; i <= static_cast<u8>(HostPhase::Failed); ++i)
        CHECK(host_phase_name(static_cast<HostPhase>(i)) != "unknown");
}
