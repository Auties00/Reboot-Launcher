# macOS play runner: the launcher-built CrossOver-source Wine 11.0 + DXMT runtime.
reboot_add_package(os_macos/runner
  OS APPLE
  SOURCES
    src/dxmt_wine_runner_platform.cpp
    src/mac_runtime_layout.cpp
    src/make_runner_platform.cpp
    src/messages.cpp
  DEPS
    reboot::posix)

# Real-filesystem suite (runtime resolution, quarantine stripping); runs on the macOS CI runner only.
if(APPLE AND REBOOT_BUILD_TESTS)
  file(GLOB conformance_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/conformance/*.cpp")
  if(conformance_sources)
    add_executable(reboot_os_macos_runner_conformance ${conformance_sources})
    _reboot_record_target(reboot_os_macos_runner_conformance test DEPS reboot::os_macos_runner reboot::testing
      SOURCES ${conformance_sources})
    target_include_directories(reboot_os_macos_runner_conformance PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(reboot_os_macos_runner_conformance PRIVATE
      reboot_os_macos_runner Catch2::Catch2WithMain reboot_warnings reboot_testing)
    if(NOT COMMAND catch_discover_tests)
      include(Catch)
    endif()
    catch_discover_tests(reboot_os_macos_runner_conformance)
  endif()
endif()
