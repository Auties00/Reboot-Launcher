#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string_view>

#include "decimal_uid.hpp"
#include "reboot/os_linux/ipc/ipc_runtime_base.hpp"
#include "reboot/ports/ipc.hpp"

using reboot::NativePath;
using reboot::os_linux::ipc::IpcRuntimeBase;
using reboot::os_linux::ipc::parse_decimal_uid;
using reboot::os_linux::ipc::resolve_ipc_runtime_base;

TEST_CASE("a uid reads back exactly as std::to_string wrote it", "[endpoint_name]") {
    CHECK(parse_decimal_uid("0") == 0u);
    CHECK(parse_decimal_uid("1000") == 1000u);
    CHECK(parse_decimal_uid("4294967294") == 4294967294u);
}

TEST_CASE("anything else is not a uid", "[endpoint_name]") {
    for (const std::string_view text : {"", "-1", "+1", "01", "1000 ", " 1000", "1e3", "abc", "4294967295",
                                        "4294967296", "S-1-5-21-1000"}) {
        INFO(text);
        CHECK_FALSE(parse_decimal_uid(text));
    }
}

TEST_CASE("an identity without a decimal uid has no endpoint", "[endpoint_name]") {
    CHECK(reboot::ports::endpoint_name({"S-1-5-21-1000", 7}, "0123456789abcdef").empty());
    CHECK(reboot::ports::endpoint_name({"", 7}, "0123456789abcdef").empty());
}

TEST_CASE("an absolute XDG_RUNTIME_DIR is the runtime base", "[endpoint_name]") {
    const IpcRuntimeBase base = resolve_ipc_runtime_base("/run/user/1000", 1000);
    CHECK(base.path == NativePath{"/run/user/1000"});
    CHECK(base.from_xdg_runtime_dir);
}

TEST_CASE("without an absolute XDG_RUNTIME_DIR the base is a per-uid /tmp directory", "[endpoint_name]") {
    for (const std::optional<std::string_view> value :
         {std::optional<std::string_view>{}, std::optional<std::string_view>{""},
          std::optional<std::string_view>{"run/user/1000"}}) {
        const IpcRuntimeBase base = resolve_ipc_runtime_base(value, 1000);
        CHECK(base.path == NativePath{"/tmp/reboot-launcher-1000"});
        CHECK_FALSE(base.from_xdg_runtime_dir);
    }
}
