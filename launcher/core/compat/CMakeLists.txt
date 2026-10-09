# Public headers use components' runtimes, process' EnvBuilder and storage's
# DocumentStore; game_channel is reached only from sources.
reboot_add_package(compat
  SOURCES
    src/compat_document.cpp
    src/messages.cpp
    src/path_mapper.cpp
    src/pe_imports.cpp
    src/preferred_rhi_seed.cpp
    src/prefix_busy.cpp
    src/prefix_files.cpp
    src/prefix_lease.cpp
    src/prefix_manager.cpp
    src/runner_env.cpp
    src/runtime_core.cpp
    src/runtime_id.cpp
    src/runtime_service.cpp
    src/wine_session_host.cpp
  PUBLIC_DEPS reboot::components reboot::process reboot::storage
  DEPS reboot::game_channel)

if(TARGET reboot_compat_tests)
  target_compile_definitions(reboot_compat_tests PRIVATE REBOOT_COMPAT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
