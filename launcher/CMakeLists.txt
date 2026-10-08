cmake_minimum_required(VERSION 3.30)
project(reboot_launcher VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
# CMake knows no C++26 flag for MSVC; its C++23 mode is /std:c++latest, MSVC's newest.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND NOT DEFINED CMAKE_CXX26_STANDARD_COMPILE_OPTION)
  set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# There are no named modules, so skip the per-source module dependency scan.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" "${CMAKE_CURRENT_SOURCE_DIR}/../server-browser/cmake")

option(REBOOT_BUILD_TESTS "Build unit, contract and conformance tests" ON)
option(REBOOT_BUILD_FAKES "Build the fake backend, game server and game executables" ON)
set(REBOOT_SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. address,undefined (GCC and Clang)")
set(REBOOT_BUILD_ID "${PROJECT_VERSION}-dev" CACHE STRING "Build identity; engine IPC requires an exact match")

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(RebootWarnings)
include(RebootPackage)
include(RebootFloor)
include(RebootDagCheck)

# Only the INTERFACE target sb_contract: wire codec, framer, core types and the view mirror.
set(SB_CLIENT_ONLY ON CACHE BOOL "" FORCE)
add_subdirectory(../server-browser "${CMAKE_BINARY_DIR}/server-browser" EXCLUDE_FROM_ALL)

if(REBOOT_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=${REBOOT_SANITIZE} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${REBOOT_SANITIZE})
endif()
add_compile_definitions(REBOOT_BUILD_ID="${REBOOT_BUILD_ID}")

find_package(Threads REQUIRED)
find_package(Boost REQUIRED CONFIG COMPONENTS headers asio beast json pfr container unordered)
find_package(OpenSSL 3.0 REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(LibArchive REQUIRED)
find_package(MsQuic REQUIRED)
find_package(miniupnpc CONFIG REQUIRED)
find_package(libnatpmp REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)
if(WIN32 OR APPLE)
  find_package(velopack CONFIG REQUIRED)
endif()

if(REBOOT_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(core/foundation)
add_subdirectory(core/ports)
add_subdirectory(core/contracts)

set(REBOOT_CORE_PACKAGES
  api posix logging trust storage ux net components catalog support builds
  process secrets identity injection game_channel compat backend front gameserver
  sessions browser publish host play updates integration
  ipc engine client testing)

if(WIN32)
  set(REBOOT_OS windows)
  set(REBOOT_OS_PACKAGES platform ipc win32session winhost)
elseif(APPLE)
  set(REBOOT_OS macos)
  set(REBOOT_OS_PACKAGES platform ipc runner)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(REBOOT_OS linux)
  set(REBOOT_OS_PACKAGES platform ipc runner)
else()
  message(FATAL_ERROR "Unsupported host OS: ${CMAKE_SYSTEM_NAME}")
endif()

foreach(package IN LISTS REBOOT_CORE_PACKAGES)
  add_subdirectory(core/${package})
endforeach()
foreach(package IN LISTS REBOOT_OS_PACKAGES)
  add_subdirectory(core-${REBOOT_OS}/${package})
endforeach()

add_library(reboot_platform INTERFACE)
add_library(reboot::platform ALIAS reboot_platform)
target_link_libraries(reboot_platform INTERFACE
  $<TARGET_NAME_IF_EXISTS:reboot_os_${REBOOT_OS}_platform>
  $<TARGET_NAME_IF_EXISTS:reboot_os_${REBOOT_OS}_ipc>
  $<TARGET_NAME_IF_EXISTS:reboot_os_${REBOOT_OS}_runner>)

add_library(reboot_client_platform INTERFACE)
add_library(reboot::client_platform ALIAS reboot_client_platform)
target_link_libraries(reboot_client_platform INTERFACE $<TARGET_NAME_IF_EXISTS:reboot_os_${REBOOT_OS}_ipc>)
