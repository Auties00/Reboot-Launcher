# Engine IPC transport (named pipe) and the client-side ports; linked by reboot-engine and by
# reboot_client through reboot::client_platform, so it reaches only the foundation and Win32.
reboot_add_package(os_windows/ipc
  OS WINDOWS
  SOURCES
    src/endpoint_name.cpp
    src/engine_environment_block.cpp
    src/messages.cpp
  DEPS
    advapi32 ole32 oleaut32 shell32 taskschd user32)

# Real-pipe suite (port_conformance's IPC and caller-context suites, squatting, anonymous clients,
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
    if(NOT COMMAND catch_discover_tests)
      include(Catch)
    endif()
    catch_discover_tests(reboot_os_windows_ipc_conformance)
  endif()
endif()
