# The suspended-spawn + Job + remote-thread injection primitives shared by Win32SessionHost (engine,
# native Windows) and reboot-winhost.exe (under Wine). Not reboot_add_package: that links
# reboot::foundation (Boost.Asio, the message registry), but this library must build /MT for the
# winhost executable, so it links only reboot::contracts plus the Win32 libraries it calls.
if(NOT WIN32)
  return()
endif()

set(win32session_runtime "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(reboot_os_windows_win32session STATIC
  src/file_park.cpp
  src/remote_injector.cpp
  src/wide.cpp
  src/win32_session.cpp)
add_library(reboot::os_windows_win32session ALIAS reboot_os_windows_win32session)
target_include_directories(reboot_os_windows_win32session
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# bcrypt hashes the pinned DLLs without OpenSSL; psapi enumerates modules to confirm a load.
target_link_libraries(reboot_os_windows_win32session
  PUBLIC reboot::contracts
  PRIVATE bcrypt psapi reboot_warnings)
set_target_properties(reboot_os_windows_win32session PROPERTIES MSVC_RUNTIME_LIBRARY "${win32session_runtime}")
_reboot_record_target(reboot_os_windows_win32session package DEPS reboot::contracts)

if(REBOOT_BUILD_TESTS)
  file(GLOB win32session_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
  if(win32session_tests)
    add_executable(reboot_os_windows_win32session_tests ${win32session_tests})
    target_include_directories(reboot_os_windows_win32session_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(reboot_os_windows_win32session_tests PRIVATE
      reboot_os_windows_win32session Catch2::Catch2WithMain reboot_warnings)
    set_target_properties(reboot_os_windows_win32session_tests PROPERTIES
      MSVC_RUNTIME_LIBRARY "${win32session_runtime}")
    if(NOT COMMAND catch_discover_tests)
      include(Catch)
    endif()
    catch_discover_tests(reboot_os_windows_win32session_tests)
  endif()
endif()
