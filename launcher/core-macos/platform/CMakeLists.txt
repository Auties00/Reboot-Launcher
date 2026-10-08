# macOS adapters for every engine port, and ports::make_platform().
if(NOT APPLE)
  return()
endif()

reboot_add_package(os_macos/platform
  OS APPLE
  SOURCES
    src/messages.cpp
    src/watchdog_script.cpp
  PUBLIC_DEPS
    reboot::posix
  DEPS
    velopack::velopack
    "-framework AppKit" "-framework CoreFoundation" "-framework CoreServices" "-framework Foundation"
    "-framework Metal" "-framework Security" "-framework ServiceManagement")

# The Objective-C++ shims (src/*.mm) build as CXX, so no consumer directory needs OBJCXX enabled.
get_target_property(platform_sources reboot_os_macos_platform SOURCES)
list(FILTER platform_sources INCLUDE REGEX "\\.mm$")
if(platform_sources)
  set_source_files_properties(${platform_sources} PROPERTIES
    LANGUAGE CXX
    COMPILE_OPTIONS "-xobjective-c++;-fobjc-arc")
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
