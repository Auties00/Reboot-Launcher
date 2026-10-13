# Engine IPC transport (AF_UNIX) and the client-side ports; linked by reboot-engine and by
# reboot_client through reboot::client_platform, so it reaches only the foundation, posix and libc.
reboot_add_package(os_linux/ipc
  OS LINUX
  SOURCES
    src/caller_facts.cpp
    src/client_path_rules.cpp
    src/decimal_uid.cpp
    src/detached_spawn.cpp
    src/endpoint_name.cpp
    src/engine_endpoint.cpp
    src/engine_environment.cpp
    src/engine_socket_path.cpp
    src/engine_start_plan.cpp
    src/ipc_runtime_base.cpp
    src/linux_caller_context.cpp
    src/linux_client_paths.cpp
    src/linux_peer_credentials.cpp
    src/make_client_platform.cpp
    src/messages.cpp
    src/runtime_base_check.cpp
    src/socket_activation.cpp
    src/state_locks.cpp
    src/steam_reaper.cpp
    src/systemd_engine_starter.cpp
    src/systemd_unit_state.cpp
    src/tool_run.cpp
    src/unix_socket_connector.cpp
    src/unix_socket_listener.cpp
  PUBLIC_DEPS
    reboot::posix
  DEPS
    ${CMAKE_DL_LIBS})

# Real-socket engine starter suite against a fake engine; runs on the Linux CI runner only.
if(REBOOT_BUILD_TESTS)
  set(conformance_sources conformance/engine_starter_conformance.cpp)
  add_executable(reboot_os_linux_ipc_conformance ${conformance_sources})
  _reboot_record_target(reboot_os_linux_ipc_conformance test DEPS reboot::os_linux_ipc reboot::testing
    SOURCES ${conformance_sources})
  target_include_directories(reboot_os_linux_ipc_conformance PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  target_link_libraries(reboot_os_linux_ipc_conformance PRIVATE
    reboot_os_linux_ipc Catch2::Catch2WithMain reboot_warnings $<TARGET_NAME_IF_EXISTS:reboot_testing>)
  # Stands in for reboot-engine in the engine starter check.
  add_executable(reboot-fake-unix-engine conformance/fake_engine/fake_unix_engine_main.cpp)
  _reboot_record_target(reboot-fake-unix-engine test DEPS reboot::os_linux_ipc
    SOURCES conformance/fake_engine/fake_unix_engine_main.cpp)
  target_include_directories(reboot-fake-unix-engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(reboot-fake-unix-engine PRIVATE reboot_os_linux_ipc reboot_warnings)
  add_dependencies(reboot_os_linux_ipc_conformance reboot-fake-unix-engine)
  target_compile_definitions(reboot_os_linux_ipc_conformance PRIVATE
    REBOOT_FAKE_UNIX_ENGINE_EXE="$<TARGET_FILE:reboot-fake-unix-engine>")
  if(NOT COMMAND catch_discover_tests)
    include(Catch)
  endif()
  catch_discover_tests(reboot_os_linux_ipc_conformance)
endif()
