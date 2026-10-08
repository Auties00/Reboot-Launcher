# reboot-winhost.exe: an x64 PE that runs only inside the Wine prefix. It ships as a payload
# component pinned by sha256 and is never Authenticode-signed (Velopack --signExclude covers it).
# Not reboot_add_package: that links reboot::foundation, and winhost may use only the
# header-only foundation parts, so it builds /MT without Boost.Asio or a message registry.
if(NOT WIN32)
  return()
endif()

# main.cpp lands with the implementation; until then only the headers exist.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
  return()
endif()
if(NOT MSVC OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
  message(FATAL_ERROR "reboot-winhost is an x64 MSVC build")
endif()

set(REBOOT_WINHOST_BUILD "payload-dev" CACHE STRING "reboot-winhost's payload release, sent in WhHello.build")

# The sha256 pin and the public PDB need byte-identical rebuilds: no timestamps, and source
# paths mapped to one root. /pathmap with /Zi also maps the /Fd path and fails, hence /Z7.
cmake_path(GET CMAKE_SOURCE_DIR PARENT_PATH winhost_repo_root)
file(TO_NATIVE_PATH "${winhost_repo_root}" winhost_repo_root)
set(winhost_repro_options /Brepro /experimental:deterministic "/pathmap:${winhost_repo_root}=\\reboot")

add_library(reboot_os_windows_winhost STATIC
  src/control_connection.cpp
  src/winhost_agent.cpp
  src/winhost_bootstrap.cpp
  src/winhost_failure.cpp)
add_library(reboot::os_windows_winhost ALIAS reboot_os_windows_winhost)
target_include_directories(reboot_os_windows_winhost
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(reboot_os_windows_winhost
  PUBLIC reboot::contracts
  PRIVATE reboot::os_windows_win32session ws2_32 reboot_warnings)
target_compile_definitions(reboot_os_windows_winhost PRIVATE REBOOT_WINHOST_BUILD="${REBOOT_WINHOST_BUILD}")
target_compile_options(reboot_os_windows_winhost PRIVATE ${winhost_repro_options})
set_target_properties(reboot_os_windows_winhost PROPERTIES MSVC_DEBUG_INFORMATION_FORMAT Embedded)
_reboot_record_target(reboot_os_windows_winhost package DEPS reboot::os_windows_win32session)

add_executable(reboot-winhost src/main.cpp)
target_link_libraries(reboot-winhost PRIVATE reboot_os_windows_winhost reboot_warnings)
target_compile_options(reboot-winhost PRIVATE ${winhost_repro_options})
target_link_options(reboot-winhost PRIVATE /DEBUG /Brepro /OPT:REF /OPT:ICF /PDBALTPATH:%_PDB%)
set_target_properties(reboot-winhost PROPERTIES MSVC_DEBUG_INFORMATION_FORMAT Embedded)

if(REBOOT_BUILD_TESTS)
  file(GLOB winhost_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
  if(winhost_tests)
    add_executable(reboot_os_windows_winhost_tests ${winhost_tests})
    target_include_directories(reboot_os_windows_winhost_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(reboot_os_windows_winhost_tests PRIVATE
      reboot_os_windows_winhost Catch2::Catch2WithMain reboot_warnings)
    if(NOT COMMAND catch_discover_tests)
      include(Catch)
    endif()
    catch_discover_tests(reboot_os_windows_winhost_tests)
  endif()
endif()
