# Windows adapters for every engine port, and ports::make_platform().
# An unknown reboot::os_windows_win32session would fail the generate step, so wait for it to land.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../win32session/CMakeLists.txt")
  return()
endif()

reboot_add_package(os_windows/platform
  OS WINDOWS
  SOURCES
    src/credential_manager_store.cpp
    src/dpapi_file.cpp
    src/file_ops.cpp
    src/https_url.cpp
    src/job_process_launcher.cpp
    src/make_platform.cpp
    src/messages.cpp
    src/os_version.cpp
    src/process_token.cpp
    src/registrar_rules.cpp
    src/session_mapping.cpp
    src/socket_rows.cpp
    src/unsupported_peer_inspector.cpp
    src/velopack_applier.cpp
    src/velopack_layout.cpp
    src/wide.cpp
    src/win32_session_host.cpp
    src/win_error.cpp
    src/windows_disk_info.cpp
    src/windows_file_system.cpp
    src/windows_file_watcher.cpp
    src/windows_integration_registrar.cpp
    src/windows_log_file_system.cpp
    src/windows_paths.cpp
    src/windows_port_inspector.cpp
    src/windows_prerequisites.cpp
    src/windows_resolver.cpp
    src/windows_shell.cpp
    src/windows_system_info.cpp
    src/wmi_security_product_probe.cpp
  DEPS
    reboot::os_windows_win32session
    velopack::velopack
    advapi32 crypt32 iphlpapi ntdll ole32 oleaut32 shell32 taskschd userenv wbemuuid ws2_32)

# Real-adapter suite (atomic replace, lock exclusivity, watcher delivery, Job tree kill); runs on
# the Windows CI runner only.
if(WIN32 AND REBOOT_BUILD_TESTS)
  file(GLOB conformance_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/conformance/*.cpp")
  if(conformance_sources)
    add_executable(reboot_os_windows_conformance ${conformance_sources})
    _reboot_record_target(reboot_os_windows_conformance test DEPS reboot::os_windows_platform reboot::testing
      SOURCES ${conformance_sources})
    target_include_directories(reboot_os_windows_conformance PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(reboot_os_windows_conformance PRIVATE
      reboot_os_windows_platform Catch2::Catch2WithMain reboot_warnings
      $<TARGET_NAME_IF_EXISTS:reboot_testing>)
    if(NOT COMMAND catch_discover_tests)
      include(Catch)
    endif()
    # The session host case runs it.
    if(TARGET reboot-fake-game)
      add_dependencies(reboot_os_windows_conformance reboot-fake-game)
    endif()
    catch_discover_tests(reboot_os_windows_conformance)
  endif()
endif()
