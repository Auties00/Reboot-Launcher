# The engine side of reboot.api.v1: the generated handler interfaces and dispatch, the api.* messages
# and the checks that the schema mirrors the C++ enums and the frozen bootstrap ids. Only the engine
# links it. The generated files keep namespace rb::api, as the server half of the api types.
reboot_add_package(api_dispatch
  SOURCES
    src/bootstrap_ids.cpp
    src/decode_diagnostic.cpp
    src/dispatch.cpp
    src/enum_mirrors.cpp
    src/messages.cpp
  PUBLIC_DEPS reboot::api)
if(TARGET reboot_api_dispatch_tests)
  # The dispatch tests replay the api package's golden requests.
  target_compile_definitions(reboot_api_dispatch_tests PRIVATE
    REBOOT_API_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../api/tests/data")
endif()
