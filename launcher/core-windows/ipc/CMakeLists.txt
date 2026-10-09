# Engine IPC transport (named pipe) and the client-side ports; linked by reboot-engine and by
# reboot_client through reboot::client_platform, so it reaches only the foundation and Win32.
reboot_add_package(os_windows/ipc
  OS WINDOWS
  SOURCES
    src/endpoint_name.cpp
    src/engine_endpoint.cpp
    src/engine_environment_block.cpp
    src/engine_launch.cpp
    src/engine_task.cpp
    src/make_client_platform.cpp
    src/messages.cpp
    src/named_pipe_connector.cpp
    src/named_pipe_listener.cpp
    src/pipe_io_thread.cpp
    src/pipe_stream.cpp
    src/pipe_trust.cpp
    src/spawn_lock.cpp
    src/token_info.cpp
    src/wide.cpp
    src/win32_errors.cpp
    src/windows_caller_context.cpp
    src/windows_client_paths.cpp
    src/windows_engine_starter.cpp
    src/windows_file_revision_reader.cpp
  DEPS
    advapi32 ole32 oleaut32 shell32 taskschd user32)

# Real-pipe suite (port_conformance's IPC, caller-context and engine starter suites, squatting, anonymous clients,
# other-user peers when the runner provides a second account); runs on the Windows CI runner only.
if(WIN32 AND REBOOT_BUILD_TESTS)
  file(GLOB conformance_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/conformance/*.cpp")
  if(conformance_sources)
    add_executable(reboot_os_windows_ipc_conformance ${conformance_sources})
    _reboot_record_target(reboot_os_windows_ipc_conformance test DEPS reboot::os_windows_ipc reboot::testing
      SOURCES ${conformance_sources})
    target_include_directories(reboot_os_windows_ipc_conformance PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(reboot_os_windows_ipc_conformance PRIVATE
      reboot_os_windows_ipc advapi32 Catch2::Catch2WithMain reboot_warnings
      $<TARGET_NAME_IF_EXISTS:reboot_testing>)
    # Stands in for reboot-engine in the engine starter check.
    add_executable(reboot-fake-pipe-engine conformance/fake_engine/fake_pipe_engine_main.cpp)
    _reboot_record_target(reboot-fake-pipe-engine test DEPS reboot::os_windows_ipc
      SOURCES conformance/fake_engine/fake_pipe_engine_main.cpp)
    target_include_directories(reboot-fake-pipe-engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(reboot-fake-pipe-engine PRIVATE reboot_os_windows_ipc reboot_warnings)
    add_dependencies(reboot_os_windows_ipc_conformance reboot-fake-pipe-engine)
    target_compile_definitions(reboot_os_windows_ipc_conformance PRIVATE
      REBOOT_FAKE_PIPE_ENGINE_EXE="$<TARGET_FILE:reboot-fake-pipe-engine>")
    if(NOT COMMAND catch_discover_tests)
      include(Catch)
    endif()
    catch_discover_tests(reboot_os_windows_ipc_conformance)
  endif()
endif()
