#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "builds_error.hpp"
#include "builds_test_support.hpp"
#include "pe_error.hpp"
#include "reboot/builds/memory_byte_source.hpp"
#include "reboot/builds/pe_version_reader.hpp"
#include "reboot/foundation/cancel.hpp"

using namespace rb;
using namespace rb::builds;
using namespace rb::builds::test;

namespace {

const MessageSpec* spec_of(std::string_view id) {
    for (const MessageSpec* spec : message_registry())
        if (spec->id == id) return spec;
    return nullptr;
}

// Every placeholder of the message is an arg, and every arg a placeholder.
void check_args(const Diagnostic& diag) {
    INFO(diag.id);
    const MessageSpec* spec = spec_of(diag.id);
    REQUIRE(spec != nullptr);
    CHECK(diag.domain == ErrorDomain::Builds);
    CHECK(diag.args.size() == spec->args.size());
    for (const ArgSpec& arg : spec->args) CHECK(diag.find_arg(arg.name) != nullptr);
}

}  // namespace

TEST_CASE("every builds error names exactly the args its message uses") {
    for (u8 code = 0; code <= static_cast<u8>(BuildsErrorCode::Cancelled); ++code) {
        const Diagnostic diag = to_diagnostic(BuildsError{.code = static_cast<BuildsErrorCode>(code),
                                                          .path = NativePath("p"),
                                                          .other_path = NativePath("o"),
                                                          .name = "n",
                                                          .build = BuildId{},
                                                          .entry = "e",
                                                          .archive_entry = "a",
                                                          .fs_type = "FAT32",
                                                          .needed_bytes = 2,
                                                          .free_bytes = 1,
                                                          .version = version("1.2"),
                                                          .found_version = version("3.4"),
                                                          .count = 2});
        check_args(diag);
    }
    for (u8 code = 0; code <= static_cast<u8>(PeErrorCode::Cancelled); ++code)
        check_args(to_diagnostic(PeError{.code = static_cast<PeErrorCode>(code), .offset = 7}));
}

TEST_CASE("os errors and causes travel with the diagnostic") {
    const Diagnostic diag = to_diagnostic(BuildsError{.code = BuildsErrorCode::Io,
                                                      .path = NativePath("p"),
                                                      .os_error = SystemError{.code = 5},
                                                      .cause = internal_bug("cause")});
    CHECK(diag.os_error == SystemError{.code = 5});
    REQUIRE(diag.causes.size() == 1);
    CHECK(diag.causes[0].id == "internal.bug");
    CHECK(to_diagnostic(BuildsError{.code = BuildsErrorCode::Cancelled}).kind == ErrorKind::Cancelled);
    CHECK(to_diagnostic(BuildsError{.code = BuildsErrorCode::NotFound}).kind == ErrorKind::NotFound);
}

TEST_CASE("no single damaged byte in the headers sends the PE reader outside the input") {
    const std::vector<u8> good = make_pe(version_blob("4.20.0-4008490+++Fortnite+Release-3.5"));
    const PeVersionReader reader;
    const std::size_t headers = std::min<std::size_t>(good.size(), 0x200 + 96);
    for (std::size_t at = 0; at < headers; ++at) {
        for (const u8 mask : {u8{0xFF}, u8{0x80}, u8{0x01}}) {
            std::vector<u8> bytes = good;
            bytes[at] ^= mask;
            MemoryByteSource source(bytes);
            if (auto resource = reader.read_version_resource(source)) CHECK(resource->size() <= kVersionResourceCap);
            (void)reader.read_marker(source);
            (void)reader.scan_for_marker(source, CancelToken{});
        }
    }
}
