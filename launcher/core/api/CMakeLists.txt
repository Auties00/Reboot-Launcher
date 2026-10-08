# reboot.api.v1 and its generated C++. Only the engine and core/testing link this package;
# reboot_client never does, because it passes payloads through undecoded.
# include/reboot/api/v1, src/dispatch.cpp and the client's reboot/client_methods.h are generated
# from launcher/schema. Each generated
# header holds every type of one .proto file, the one exception to one main type per header.
reboot_add_package(api
  SOURCES
    src/bootstrap_ids.cpp
    src/decode_error.cpp
    src/dispatch.cpp
    src/enum_mirrors.cpp
    src/messages.cpp)
if(TARGET reboot_api_tests)
  target_compile_definitions(reboot_api_tests PRIVATE REBOOT_API_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
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
