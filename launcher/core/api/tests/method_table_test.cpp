#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include "reboot/api/v1/method_table.hpp"

namespace api = rb::api;

TEST_CASE("MethodTable is sorted, unique and searchable", "[methods]") {
    const auto methods = api::MethodTable::all();
    REQUIRE_FALSE(methods.empty());
    for (std::size_t i = 1; i < methods.size(); ++i) CHECK(methods[i - 1].id < methods[i].id);
    for (const api::MethodSpec& spec : methods) CHECK(api::MethodTable::find(spec.id) == &spec);
    CHECK(api::MethodTable::find(0) == nullptr);
    CHECK(api::MethodTable::find(0xFFFFFFFF) == nullptr);
}

TEST_CASE("installs, play starts and host starts outlive their connection", "[methods]") {
    for (const rb::u32 id : {api::kInstallInstall, api::kPlayStart, api::kHostStart}) {
        const api::MethodSpec* spec = api::MethodTable::find(id);
        REQUIRE(spec);
        CHECK(spec->kind == api::MethodKind::Operation);
        CHECK(spec->default_disconnect == rb::DisconnectPolicy::Detached);
    }
}

TEST_CASE("calls are bound to their connection and report no progress", "[methods]") {
    for (const api::MethodSpec& spec : api::MethodTable::all()) {
        if (spec.kind != api::MethodKind::Call) continue;
        CHECK(spec.default_disconnect == rb::DisconnectPolicy::BoundToConnection);
        CHECK(spec.progress == api::ProgressUnit::NoProgress);
    }
}
