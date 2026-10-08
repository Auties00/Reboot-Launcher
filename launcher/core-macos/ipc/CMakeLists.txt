# Engine IPC transport (AF_UNIX) and the client-side ports; linked by reboot-engine and by
# reboot_client through reboot::client_platform, so it reaches only the foundation, posix and
# system frameworks.
if(NOT APPLE)
  return()
endif()

# SMAppService has only an Objective-C API, so the starter is Objective-C++ (src/*.mm).
enable_language(OBJCXX)

reboot_add_package(os_macos/ipc
  OS APPLE
  SOURCES
    src/darwin_user_temp_dir.cpp
    src/endpoint_name.cpp
    src/engine_socket_path.cpp
    src/messages.cpp
  PUBLIC_DEPS
    reboot::posix
  DEPS
    "-framework Foundation" "-framework Security" "-framework ServiceManagement")
target_compile_options(reboot_os_macos_ipc PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
