# Linux adapters for every engine port, and ports::make_platform().
# Depends on: posix.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  return()
endif()

# libsecret and libgio are loaded with dlopen at run time, so neither is a link dependency.
reboot_add_package(os_linux/platform
  OS LINUX
  SOURCES
    src/messages.cpp
  PUBLIC_DEPS
    reboot::posix
  DEPS
    LibArchive::LibArchive
    ${CMAKE_DL_LIBS})

# Real-adapter suite (atomic replace, OFD lock exclusivity, inotify delivery, pidfd tree and
# scope kill, sock_diag owners); runs on the Linux CI runner only.
if(REBOOT_BUILD_TESTS)
  file(GLOB conformance_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/conformance/*.cpp")
  if(conformance_sources)
    add_executable(reboot_os_linux_conformance ${conformance_sources})
    _reboot_record_target(reboot_os_linux_conformance test DEPS reboot::os_linux_platform reboot::testing
      SOURCES ${conformance_sources})
    target_include_directories(reboot_os_linux_conformance PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(reboot_os_linux_conformance PRIVATE
      reboot_os_linux_platform Catch2::Catch2WithMain reboot_warnings
      $<TARGET_NAME_IF_EXISTS:reboot_testing>)
    if(NOT COMMAND catch_discover_tests)
      include(Catch)
    endif()
    catch_discover_tests(reboot_os_linux_conformance)
  endif()
endif()
