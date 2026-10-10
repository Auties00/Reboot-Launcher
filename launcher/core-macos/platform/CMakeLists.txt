# macOS adapters for every engine port, and ports::make_platform().
if(NOT APPLE)
  return()
endif()

reboot_add_package(os_macos/platform
  OS APPLE
  SOURCES
    src/apple_shims.mm
    src/bundle_layout.cpp
    src/dns_answers.cpp
    src/firewall_output.cpp
    src/fs_event_change.cpp
    src/fs_events_watcher.cpp
    src/full_fsync.cpp
    src/https_url.cpp
    src/integration_rules.cpp
    src/keychain_secret_store.cpp
    src/libproc_port_inspector.cpp
    src/mac_disk_info.cpp
    src/mac_file_system.cpp
    src/mac_integration_registrar.cpp
    src/mac_log_file_system.cpp
    src/mac_paths.cpp
    src/mac_prerequisite_probe.cpp
    src/mac_resolver.cpp
    src/mac_shell.cpp
    src/mac_system_info.cpp
    src/mac_velopack_applier.cpp
    src/macos_version.cpp
    src/make_platform.cpp
    src/messages.cpp
    src/pem.cpp
    src/posix_spawn_launcher.cpp
    src/proc_info.cpp
    src/run_program.cpp
    src/secret_files.cpp
    src/secret_routing.cpp
    src/socket_match.cpp
    src/sysctl_value.cpp
    src/trust_rules.cpp
    src/unsupported_peer_inspector.cpp
    src/volume_rules.cpp
    src/wait_status.cpp
    src/watchdog_script.cpp
  PUBLIC_DEPS
    reboot::posix
  DEPS
    velopack::velopack
    "-framework AppKit" "-framework CoreFoundation" "-framework CoreServices" "-framework Foundation"
    "-framework Metal" "-framework Security" "-framework ServiceManagement")

# The Objective-C++ shims (src/*.mm) build as CXX, so no consumer directory needs OBJCXX enabled.
# They build as C++23: AppKit's headers mix enumeration types in arithmetic, which C++26 made ill-formed.
get_target_property(platform_sources reboot_os_macos_platform SOURCES)
list(FILTER platform_sources INCLUDE REGEX "\\.mm$")
if(platform_sources)
  set_source_files_properties(${platform_sources} PROPERTIES
    LANGUAGE CXX
    COMPILE_OPTIONS "-xobjective-c++;-fobjc-arc;-std=c++23")
endif()

# Real-adapter suite (atomic replace, lock exclusivity, FSEvents delivery, process-group kill);
# runs on the macOS CI runner only.
if(REBOOT_BUILD_TESTS)
  file(GLOB conformance_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/conformance/*.cpp")
  if(conformance_sources)
    add_executable(reboot_os_macos_conformance ${conformance_sources})
    _reboot_record_target(reboot_os_macos_conformance test DEPS reboot::os_macos_platform reboot::testing
      SOURCES ${conformance_sources})
    target_include_directories(reboot_os_macos_conformance PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(reboot_os_macos_conformance PRIVATE
      reboot_os_macos_platform Catch2::Catch2WithMain reboot_warnings
      $<TARGET_NAME_IF_EXISTS:reboot_testing>)
    if(NOT COMMAND catch_discover_tests)
      include(Catch)
    endif()
    catch_discover_tests(reboot_os_macos_conformance)
  endif()
endif()
