# Linux play runner: umu 1.4.4 + GE-Proton, and the pinned Kron4ek Wine runner for CI.
# Depends on: posix.
reboot_add_package(os_linux/runner
  OS LINUX
  SOURCES
    src/env_vars.cpp
    src/kron_wine_runner.cpp
    src/linux_runner_platform.cpp
    src/make_runner_platform.cpp
    src/messages.cpp
    src/runtime_files.cpp
    src/runner_platform_port.cpp
    src/slr_build.cpp
    src/slr_setup.cpp
    src/umu_invocation.cpp
  DEPS
    reboot::posix)

# Real-filesystem and real-process suite (runtime resolution, SLR build reading, the setup run through a scripted
# umu-run and through PidfdProcessLauncher); runs on the Linux CI runner only.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND REBOOT_BUILD_TESTS)
  file(GLOB conformance_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/conformance/*.cpp")
  if(conformance_sources)
    add_executable(reboot_os_linux_runner_conformance ${conformance_sources})
    _reboot_record_target(reboot_os_linux_runner_conformance test
      DEPS reboot::os_linux_runner reboot::os_linux_platform reboot::testing
      SOURCES ${conformance_sources})
    target_include_directories(reboot_os_linux_runner_conformance PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(reboot_os_linux_runner_conformance PRIVATE
      reboot_os_linux_runner reboot_os_linux_platform Catch2::Catch2WithMain reboot_warnings reboot_testing)
    if(NOT COMMAND catch_discover_tests)
      include(Catch)
    endif()
    catch_discover_tests(reboot_os_linux_runner_conformance)
  endif()
endif()
