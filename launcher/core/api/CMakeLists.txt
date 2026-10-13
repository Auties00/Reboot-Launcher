# reboot.api.v1 types: the generated aggregates, enums and method table, and the codec. Header-only
# on reboot::base, so the SDK and every client compile it with their own toolchain; the engine-side
# handler interfaces and dispatch live in api_dispatch. reboot_client never includes it, because it
# passes payloads through undecoded.
# include/reboot/api/v1, ../api_dispatch's generated files, the client's reboot/client_methods.h are
# generated from launcher/schema. Each generated header holds every type of one .proto file, the one
# exception to one main type per header.
add_library(reboot_api INTERFACE)
add_library(reboot::api ALIAS reboot_api)
target_include_directories(reboot_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(reboot_api INTERFACE reboot::base)
_reboot_record_target(reboot_api lean)

if(REBOOT_BUILD_TESTS)
  add_executable(reboot_api_tests tests/golden_test.cpp tests/method_table_test.cpp tests/wire_mirror_test.cpp)
  target_link_libraries(reboot_api_tests PRIVATE reboot_api reboot::contracts Catch2::Catch2WithMain reboot_warnings)
  target_compile_definitions(reboot_api_tests PRIVATE REBOOT_API_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  if(NOT COMMAND catch_discover_tests)
    include(Catch)
  endif()
  catch_discover_tests(reboot_api_tests)
endif()

# Build-time tools only; the generated C++ and golden vectors are committed. protoc is vcpkg's
# host tool, so every machine generates with the same version.
set(REBOOT_SCHEMA_PROTO "${PROJECT_SOURCE_DIR}/schema/proto")
find_program(REBOOT_PROTOC protoc PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/protobuf" NO_DEFAULT_PATH)
find_program(REBOOT_CARGO cargo)
if(REBOOT_PROTOC AND REBOOT_CARGO AND Python3_Interpreter_FOUND)
  set(cargo_env "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR}/protoc-gen-reboot-cpp")
  add_custom_target(reboot_api_generate
    COMMAND ${cargo_env} "${REBOOT_CARGO}" run --quiet --release --manifest-path tools/protoc-gen-reboot-cpp/Cargo.toml --
            --protoc "${REBOOT_PROTOC}" --proto-root "${REBOOT_SCHEMA_PROTO}" --out ..
    COMMAND Python3::Interpreter tools/make_golden.py --protoc "${REBOOT_PROTOC}"
            --proto-root "${REBOOT_SCHEMA_PROTO}" --data-dir tests/data
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Regenerating reboot.api.v1 C++ and golden vectors"
    VERBATIM)
  add_custom_target(reboot_api_check
    COMMAND ${cargo_env} "$<TARGET_FILE:Python3::Interpreter>" tools/check_schema.py --protoc "${REBOOT_PROTOC}" --cargo "${REBOOT_CARGO}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Checking reboot.api.v1: generated code, golden vectors, buf lint and buf breaking"
    VERBATIM)
endif()
