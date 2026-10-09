#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/injection/dll_path_validator.hpp"
#include "reboot/injection/inject_failed.hpp"
#include "reboot/testing/in_memory_file_system.hpp"

using namespace rb;
using namespace rb::injection;

namespace {

std::vector<u8> golden(std::string_view name) {
    std::ifstream stream(std::string(REBOOT_INJECTION_TEST_DATA) + "/" + std::string(name), std::ios::binary);
    REQUIRE(stream);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

const NativePath kDll{"auth/custom.dll"};

}  // namespace

TEST_CASE("check_name refuses an empty path, then a name without .dll", "[injection][validator]") {
    const auto empty = DllPathValidator::check_name(NativePath{});
    REQUIRE_FALSE(empty);
    CHECK(empty.error().is(msg::kDllPathEmpty));

    const auto exe = DllPathValidator::check_name("game/FortniteClient.exe");
    REQUIRE_FALSE(exe);
    CHECK(exe.error().is(msg::kDllNotDll));
    CHECK(exe.error().find_arg("path") != nullptr);

    CHECK(DllPathValidator::check_name("auth/Cobalt.DLL"));
    CHECK(DllPathValidator::check_name(kDll));
}

TEST_CASE("check_image accepts only an x64 PE32+ image with the DLL characteristic", "[injection][validator]") {
    CHECK(DllPathValidator::check_image(kDll, golden("pe64_dll.bin")));

    const auto exe = DllPathValidator::check_image(kDll, golden("pe64_exe.bin"));
    REQUIRE_FALSE(exe);
    CHECK(exe.error().is(msg::kDllNotDll));

    const auto pe32 = DllPathValidator::check_image(kDll, golden("pe32_dll.bin"));
    REQUIRE_FALSE(pe32);
    CHECK(pe32.error().is(msg::kDllNotPe64));

    const std::vector<u8> text{'n', 'o', 't', ' ', 'a', ' ', 'p', 'e'};
    CHECK(DllPathValidator::check_image(kDll, text).error().is(msg::kDllNotPe64));

    std::vector<u8> truncated = golden("pe64_dll.bin");
    truncated.resize(0x90);
    CHECK(DllPathValidator::check_image(kDll, truncated).error().is(msg::kDllNotPe64));
}

TEST_CASE("check_image refuses a foreign machine, a bad signature and an out-of-range header offset",
          "[injection][validator]") {
    // The golden DLL's PE header is at 0x80.
    std::vector<u8> arm64 = golden("pe64_dll.bin");
    arm64[0x84] = 0x64;
    arm64[0x85] = 0xAA;
    CHECK(DllPathValidator::check_image(kDll, arm64).error().is(msg::kDllNotPe64));

    std::vector<u8> no_signature = golden("pe64_dll.bin");
    no_signature[0x81] = 'X';
    CHECK(DllPathValidator::check_image(kDll, no_signature).error().is(msg::kDllNotPe64));

    std::vector<u8> no_mz = golden("pe64_dll.bin");
    no_mz[0] = 'N';
    CHECK(DllPathValidator::check_image(kDll, no_mz).error().is(msg::kDllNotPe64));

    for (const u8 top : {u8{0x00}, u8{0xFF}}) {
        std::vector<u8> far = golden("pe64_dll.bin");
        far[0x3C] = 0xF0;
        far[0x3D] = 0xFF;
        far[0x3E] = top;
        far[0x3F] = top;
        CHECK(DllPathValidator::check_image(kDll, far).error().is(msg::kDllNotPe64));
    }
}

TEST_CASE("validate runs the name checks before reading and the image checks after", "[injection][validator]") {
    testing::InMemoryFileSystem fs;
    const DllPathValidator validator(fs);

    CHECK(validator.validate(NativePath{}).error().is(msg::kDllPathEmpty));
    CHECK(validator.validate("auth/missing.exe").error().is(msg::kDllNotDll));

    fs.write(kDll, golden("pe32_dll.bin"));
    CHECK(validator.validate(kDll).error().is(msg::kDllNotPe64));
    fs.write(kDll, golden("pe64_exe.bin"));
    CHECK(validator.validate(kDll).error().is(msg::kDllNotDll));
    CHECK(validator.resolve(kDll).error().is(msg::kDllNotDll));
}

#ifdef _WIN32
TEST_CASE("check_name refuses an extension with an unpaired surrogate without throwing", "[injection][validator]") {
    const NativePath path{std::wstring{L"auth/custom.dl"} + wchar_t{0xD800}};
    const auto result = DllPathValidator::check_name(path);
    REQUIRE_FALSE(result);
    CHECK(result.error().is(msg::kDllNotDll));
}
#endif

TEST_CASE("validate pins the DLL with the digest of the bytes it read", "[injection][validator]") {
    testing::InMemoryFileSystem fs;
    const std::vector<u8> image = golden("pe64_dll.bin");
    fs.write(kDll, image);
    const DllPathValidator validator(fs);

    const auto pinned = validator.validate(kDll);
    REQUIRE(pinned);
    CHECK(pinned->path == kDll);
    CHECK(pinned->sha256 == sha256(image));

    const auto resolved = validator.resolve(kDll);
    REQUIRE(resolved);
    CHECK(*resolved == std::optional<PinnedDll>{*pinned});
}

TEST_CASE("resolve without a custom auth DLL means our DLL only", "[injection][validator]") {
    testing::InMemoryFileSystem fs;
    const auto resolved = DllPathValidator(fs).resolve(std::nullopt);
    REQUIRE(resolved);
    CHECK_FALSE(resolved->has_value());
}

TEST_CASE("validate reports a file that does not exist as missing", "[injection][validator]") {
    testing::InMemoryFileSystem fs;
    const auto missing = DllPathValidator(fs).validate(kDll);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().is(msg::kDllMissing));
    CHECK(missing.error().kind == ErrorKind::NotFound);
}

TEST_CASE("an unreadable file keeps the read's OS code and cause", "[injection][validator]") {
    testing::InMemoryFileSystem fs;
    fs.write(kDll, golden("pe64_dll.bin"));
    const SystemError denied{SystemError::Origin::Host, 13};
    fs.faults().fail_next(testing::FsOperation::ReadAll,
                          make_diag(ErrorDomain::Posix, MessageId{"posix.io_failed"}).os(denied).build());

    const auto unreadable = DllPathValidator(fs).validate(kDll);
    REQUIRE_FALSE(unreadable);
    CHECK(unreadable.error().is(msg::kDllUnreadable));
    CHECK(unreadable.error().kind == ErrorKind::Generic);
    CHECK(unreadable.error().os_error == denied);
    REQUIRE(unreadable.error().causes.size() == 1);
    CHECK(unreadable.error().causes[0].id == "posix.io_failed");
}

TEST_CASE("a failed injection names the slot, the path and the OS code", "[injection][inject_failed]") {
    const SystemError code{SystemError::Origin::GuestWindows, 5};
    const Diagnostic diag = to_diagnostic(InjectFailed{DllSlot::CustomAuth, kDll, code});
    CHECK(diag.is(msg::kInjectFailed));
    CHECK(diag.domain == ErrorDomain::Injection);
    CHECK(diag.os_error == code);
    REQUIRE(diag.find_arg("path") != nullptr);
    const Arg* slot = diag.find_arg("slot");
    REQUIRE(slot != nullptr);
    CHECK(*slot == Arg{static_cast<u64>(DllSlot::CustomAuth)});
}
