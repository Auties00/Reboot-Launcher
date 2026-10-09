# The composition root: every domain package is a dependency. Public headers carry api, ipc, play,
# sessions, updates, process and the store documents of storage, compat, gameserver and host.
reboot_add_package(engine
  SOURCES
    src/api_convert.cpp
    src/api_requests.cpp
    src/api_router.cpp
    src/api_router_calls.cpp
    src/current_process.cpp
    src/engine_activity_probe.cpp
    src/engine_adapters.cpp
    src/engine_command_line.cpp
    src/engine_host.cpp
    src/engine_host_backend_link.cpp
    src/engine_info.cpp
    src/engine_lifecycle.cpp
    src/engine_lock.cpp
    src/engine_services.cpp
    src/engine_state_json.cpp
    src/engine_stores.cpp
    src/host_identity_own_servers.cpp
    src/host_platform.cpp
    src/idle_policy.cpp
    src/messages.cpp
    src/runtime_recorder.cpp
    src/state_extras.cpp
    src/state_guidance_store.cpp
  DEPS
    reboot::logging reboot::ux reboot::components reboot::catalog
    reboot::support reboot::builds reboot::secrets reboot::injection
    reboot::game_channel reboot::backend reboot::front reboot::browser reboot::publish
    reboot::integration
  PUBLIC_DEPS
    reboot::api reboot::ipc reboot::play reboot::sessions reboot::updates reboot::process
    reboot::storage reboot::compat reboot::gameserver reboot::host reboot::identity reboot::net
    reboot::trust)

# The OS module supplies make_platform and make_client_platform.
reboot_add_executable(reboot-engine
  SOURCES main.cpp
  DEPS reboot::engine reboot::platform)

# A console window would live as long as the engine, and closing it would kill the engine.
if(WIN32)
  set_target_properties(reboot-engine PROPERTIES WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_link_options(reboot-engine PRIVATE /ENTRY:mainCRTStartup)
  endif()
endif()

if(TARGET reboot_engine_tests)
  # The play flow tests sign their release manifest.
  target_link_libraries(reboot_engine_tests PRIVATE OpenSSL::Crypto)
endif()

# CI gates (spec 14.5), labelled "gate". The secret grep runs flows with known passwords and tokens,
# then greps every log, the ring and the export; it uses the engine tests' rig.
if(REBOOT_BUILD_TESTS)
  add_executable(reboot_engine_secret_gate gates/secret_grep_gate.cpp)
  _reboot_record_target(reboot_engine_secret_gate test
    DEPS reboot::engine reboot::api reboot::trust reboot::logging reboot::testing SOURCES gates/secret_grep_gate.cpp)
  target_include_directories(reboot_engine_secret_gate PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  target_link_libraries(reboot_engine_secret_gate PRIVATE
    reboot_engine reboot::logging reboot::testing Catch2::Catch2WithMain reboot_warnings
    OpenSSL::Crypto LibArchive::LibArchive)
  if(NOT COMMAND catch_discover_tests)
    include(Catch)
  endif()
  catch_discover_tests(reboot_engine_secret_gate PROPERTIES LABELS gate)
endif()

# Only Windows and macOS installs live in a Velopack package, which an update replaces whole.
if(WIN32 OR APPLE)
  reboot_add_executable(reboot-data-root-probe
    SOURCES gates/data_root_probe.cpp
    DEPS reboot::platform)
  if(REBOOT_BUILD_TESTS)
    add_test(NAME data_root_outside_package
      COMMAND "${CMAKE_COMMAND}" "-DPROBE=$<TARGET_FILE:reboot-data-root-probe>" "-DENGINE=$<TARGET_FILE:reboot-engine>"
              "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/data_root_gate" -P "${CMAKE_CURRENT_SOURCE_DIR}/gates/data_root_gate.cmake")
    set_tests_properties(data_root_outside_package PROPERTIES LABELS gate)
  endif()
endif()
