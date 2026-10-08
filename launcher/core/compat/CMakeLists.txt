# Public headers use components' runtimes, process' EnvBuilder and storage's
# DocumentStore; game_channel is reached only from sources.
reboot_add_package(compat
  SOURCES
    src/messages.cpp
    src/path_mapper.cpp
    src/pe_imports.cpp
    src/preferred_rhi_seed.cpp
    src/prefix_busy.cpp
    src/runtime_id.cpp
  PUBLIC_DEPS reboot::components reboot::process reboot::storage
  DEPS reboot::game_channel)

if(TARGET reboot_compat_tests)
  target_compile_definitions(reboot_compat_tests PRIVATE REBOOT_COMPAT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
