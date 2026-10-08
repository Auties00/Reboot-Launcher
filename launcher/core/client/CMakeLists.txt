# reboot_client is the one shared library and the only public C ABI. reboot_add_package builds only
# static libraries, so this package builds its sources once as an object library that both the
# shared library and the tests link. It never links api, a domain package, OpenSSL, curl, MsQuic or
# libarchive; dag_check inspects the built library's imports.
add_library(reboot_client_objects OBJECT
  src/abi_boundary.cpp
  src/api_mirrors.cpp
  src/c_api.cpp
  src/call_failure.cpp
  src/client_context.cpp
  src/client_runtime.cpp
  src/connect_settings.cpp
  src/engine_executable.cpp
  src/event_subscription.cpp
  src/messages.cpp
  src/op_table.cpp
  src/pending_calls.cpp)
target_include_directories(reboot_client_objects
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(reboot_client_objects
  PUBLIC reboot::foundation reboot::ports reboot::contracts reboot::ipc reboot::client_platform
  PRIVATE reboot_warnings)
target_compile_definitions(reboot_client_objects PRIVATE REBOOT_CLIENT_BUILD
                                                         $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
set_target_properties(reboot_client_objects PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/reboot/client.h" abi_defines REGEX "^#define RB_ABI_(MAJOR|MINOR) ")
string(REGEX REPLACE ".*RB_ABI_MAJOR ([0-9]+).*" "\\1" rb_abi_major "${abi_defines}")
string(REGEX REPLACE ".*RB_ABI_MINOR ([0-9]+).*" "\\1" rb_abi_minor "${abi_defines}")

add_library(reboot_client SHARED)
add_library(reboot::client ALIAS reboot_client)
target_include_directories(reboot_client PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(reboot_client PRIVATE reboot_client_objects)
set_target_properties(reboot_client PROPERTIES
  VERSION ${rb_abi_major}.${rb_abi_minor}.0
  SOVERSION ${rb_abi_major})
if(APPLE)
  target_link_options(reboot_client PRIVATE "LINKER:-exported_symbol,_rb_*")
elseif(NOT WIN32)
  # Keeps the static foundation's C++ symbols out of the export table.
  target_link_options(reboot_client PRIVATE "LINKER:--exclude-libs,ALL")
endif()
_reboot_record_target(reboot_client package DEPS reboot::ipc reboot::client_platform)

# A shared library links only position-independent code; the OS ipc package is added after this one.
function(reboot_client_require_pic)
  foreach(dependency IN ITEMS reboot_foundation reboot_ipc reboot_posix reboot_os_${REBOOT_OS}_ipc)
    if(TARGET ${dependency})
      set_property(TARGET ${dependency} PROPERTY POSITION_INDEPENDENT_CODE ON)
    endif()
  endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL reboot_client_require_pic)

if(REBOOT_BUILD_TESTS)
  file(GLOB client_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
  if(client_tests)
    add_executable(reboot_client_tests ${client_tests})
    target_include_directories(reboot_client_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(reboot_client_tests PRIVATE REBOOT_CLIENT_BUILD)
    target_link_libraries(reboot_client_tests PRIVATE reboot_client_objects Catch2::Catch2WithMain reboot_warnings)
    if(NOT COMMAND catch_discover_tests)
      include(Catch)
    endif()
    catch_discover_tests(reboot_client_tests)
  endif()
endif()
