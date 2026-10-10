# Engine IPC transport (AF_UNIX) and the client-side ports; linked by reboot-engine and by
# reboot_client through reboot::client_platform, so it reaches only the foundation, posix and
# system frameworks.
if(NOT APPLE)
  return()
endif()

reboot_add_package(os_macos/ipc
  OS APPLE
  SOURCES
    src/agent_service.mm
    src/caller_facts.cpp
    src/client_path_rules.cpp
    src/darwin_peer_credentials.cpp
    src/darwin_user_temp_dir.cpp
    src/endpoint_name.cpp
    src/engine_endpoint.cpp
    src/engine_socket_path.cpp
    src/engine_start_rules.cpp
    src/launchctl_run.cpp
    src/mac_caller_context.cpp
    src/mac_client_paths.cpp
    src/make_client_platform.cpp
    src/messages.cpp
    src/pending_call.cpp
    src/sm_app_service_engine_starter.cpp
    src/spawn_lock.cpp
    src/unix_socket_connector.cpp
    src/unix_socket_listener.cpp
  PUBLIC_DEPS
    reboot::posix
  DEPS
    "-framework CoreFoundation" "-framework Foundation" "-framework Security" "-framework ServiceManagement")

# SMAppService has only an Objective-C API. The shim builds as CXX, so no consumer directory needs
# OBJCXX enabled.
set_source_files_properties(src/agent_service.mm PROPERTIES
  LANGUAGE CXX
  COMPILE_OPTIONS "-xobjective-c++;-fobjc-arc")

# Real-adapter suite (socket IPC, peer checks, caller context, launchctl); runs on the macOS CI
# runner only.
if(REBOOT_BUILD_TESTS)
  file(GLOB conformance_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/conformance/*.cpp")
  if(conformance_sources)
    add_executable(reboot_os_macos_ipc_conformance ${conformance_sources})
    _reboot_record_target(reboot_os_macos_ipc_conformance test DEPS reboot::os_macos_ipc reboot::posix reboot::testing
      SOURCES ${conformance_sources})
    target_include_directories(reboot_os_macos_ipc_conformance PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(reboot_os_macos_ipc_conformance PRIVATE
      reboot_os_macos_ipc Catch2::Catch2WithMain reboot_warnings
      $<TARGET_NAME_IF_EXISTS:reboot_testing>)
    if(NOT COMMAND catch_discover_tests)
      include(Catch)
    endif()
    catch_discover_tests(reboot_os_macos_ipc_conformance)
  endif()
endif()
