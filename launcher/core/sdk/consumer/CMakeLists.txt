# A client built the way the UIs and the CLI are: from the installed SDK headers and the
# reboot_client library alone, with this toolchain's defaults (the dynamic CRT on MSVC), against
# nothing else of the launcher's build. CI configures it against `cmake --install <build> --component sdk`.
cmake_minimum_required(VERSION 3.25)
project(reboot_sdk_consumer LANGUAGES CXX)

set(REBOOT_SDK_ROOT "" CACHE PATH "Where the sdk component was installed")
if(NOT REBOOT_SDK_ROOT)
  message(FATAL_ERROR "Set REBOOT_SDK_ROOT to the installed sdk component")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The dynamic CRT a WinUI 3 app uses; reboot_client carries its own static one.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
find_package(Threads REQUIRED)

find_library(REBOOT_CLIENT_LIBRARY NAMES reboot_client PATHS "${REBOOT_SDK_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
add_executable(reboot-sdk-consumer main.cpp)
target_include_directories(reboot-sdk-consumer PRIVATE "${REBOOT_SDK_ROOT}/include")
target_link_libraries(reboot-sdk-consumer PRIVATE "${REBOOT_CLIENT_LIBRARY}" Threads::Threads)
if(MSVC)
  target_compile_options(reboot-sdk-consumer PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
else()
  target_compile_options(reboot-sdk-consumer PRIVATE -Wall -Wextra -Werror)
  set_target_properties(reboot-sdk-consumer PROPERTIES BUILD_RPATH "${REBOOT_SDK_ROOT}/lib")
endif()
if(WIN32)
  # The DLL is found next to the executable.
  add_custom_command(TARGET reboot-sdk-consumer POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${REBOOT_SDK_ROOT}/bin/reboot_client.dll" "$<TARGET_FILE_DIR:reboot-sdk-consumer>"
    VERBATIM)
endif()
