// reboot_client passes Diagnostic and Event bytes through undecoded, so the private contract
// types and the public messages must stay byte-identical. src/enum_mirrors.cpp checks the enums.
#include <catch2/catch_test_macros.hpp>

#include "reboot/api/codec.hpp"
#include "reboot/api/v1/common.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/contracts/ipc.hpp"

namespace api = reboot::api;
namespace common = reboot::contracts::common;
namespace ipc = reboot::contracts::ipc;

TEST_CASE("WireDiagnostic and api::Diagnostic share a layout", "[mirror]") {
    common::WireDiagnostic wire;
    wire.id = "host.port_busy";
    wire.args = {{"port", common::ArgKind::Unsigned, "7777"}, {"elapsed", common::ArgKind::Millis, "1500"}};
    wire.detail = "bind";
    wire.os_origin = reboot::SystemError::Origin::GuestWindows;
    wire.os_code = -10048;
    wire.retryable = true;
    wire.kind = reboot::ErrorKind::Conflict;

    const api::Bytes bytes = sb::wire::encode_to_bytes(wire);
    const auto diag = api::decode<api::Diagnostic>(bytes);
    REQUIRE(diag);
    CHECK(diag->id == wire.id);
    REQUIRE(diag->args.size() == 2);
    CHECK(diag->args[1].kind == api::ArgKind::Millis);
    CHECK(diag->os_origin == api::OsErrorOrigin::GuestWindows);
    CHECK(diag->kind == api::ErrorKind::Conflict);
    CHECK(api::encode(*diag) == bytes);
}

TEST_CASE("WireEvent and api::Event share a layout", "[mirror]") {
    ipc::WireEvent wire;
    wire.kind = static_cast<reboot::u32>(api::EventKind::SessionEnded);
    wire.epoch = 2;
    wire.seq = 99;
    wire.session = reboot::Uuid{};
    wire.op = 0;
    wire.payload = {0x62, 0x00};

    const api::Bytes bytes = sb::wire::encode_to_bytes(wire);
    const auto event = api::decode<api::Event>(bytes);
    REQUIRE(event);
    CHECK(event->kind == api::EventKind::SessionEnded);
    CHECK(event->op_id == 0u);
    CHECK(api::encode(*event) == bytes);
}
