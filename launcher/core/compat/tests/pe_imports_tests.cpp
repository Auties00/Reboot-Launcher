#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "messages.hpp"
#include "reboot/compat/pe_imports.hpp"
#include "test_data.hpp"

using namespace rb;
using namespace rb::compat;

TEST_CASE("regular and delay-loaded imports are read in order", "[compat][pe]") {
    const auto image = test::read_bytes("pe64_dynamic_crt.dll");
    const auto imports = read_pe_imports("pe64_dynamic_crt.dll", image);
    REQUIRE(imports);
    CHECK(*imports == std::vector<std::string>{"KERNEL32.dll", "MSVCP140.dll", "VCRUNTIME140_1.dll"});
    CHECK(needs_vc_runtime(*imports));
}

TEST_CASE("a static CRT image needs no VC++ runtime", "[compat][pe]") {
    const auto image = test::read_bytes("pe64_static_crt.dll");
    const auto imports = read_pe_imports("pe64_static_crt.dll", image);
    REQUIRE(imports);
    CHECK(*imports == std::vector<std::string>{"KERNEL32.dll", "USER32.dll"});
    CHECK_FALSE(needs_vc_runtime(*imports));
}

TEST_CASE("needs_vc_runtime ignores case", "[compat][pe]") {
    const std::vector<std::string> imports{"kernel32.dll", "vcruntime140.dll"};
    CHECK(needs_vc_runtime(imports));
}

TEST_CASE("a PE32 image is refused", "[compat][pe]") {
    const auto image = test::read_bytes("pe32.dll");
    const auto imports = read_pe_imports("pe32.dll", image);
    REQUIRE_FALSE(imports);
    CHECK(imports.error().is(msg::kPeMalformed));
    CHECK(imports.error().find_arg("path") != nullptr);
}

TEST_CASE("a name outside every section is refused at its descriptor", "[compat][pe]") {
    const auto image = test::read_bytes("pe64_name_outside.dll");
    const auto imports = read_pe_imports("pe64_name_outside.dll", image);
    REQUIRE_FALSE(imports);
    REQUIRE(imports.error().is(msg::kPeMalformed));
    CHECK(*imports.error().find_arg("offset") == Arg{u64{0x20C}});
}

TEST_CASE("every truncation before the last name ends is refused", "[compat][pe]") {
    const auto image = test::read_bytes("pe64_dynamic_crt.dll");
    const std::string_view last = "VCRUNTIME140_1.dll";
    const auto found = std::search(image.begin(), image.end(), last.begin(), last.end());
    REQUIRE(found != image.end());
    const auto used = static_cast<std::size_t>(found - image.begin()) + last.size() + 1;
    for (std::size_t size = 0; size < used; ++size) {
        const std::span<const u8> truncated(image.data(), size);
        const auto imports = read_pe_imports("pe64_dynamic_crt.dll", truncated);
        CHECK_FALSE(imports);
    }
}
