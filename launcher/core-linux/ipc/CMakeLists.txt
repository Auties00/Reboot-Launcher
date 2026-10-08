# Engine IPC transport (AF_UNIX) and the client-side ports; linked by reboot-engine and by
# reboot_client through reboot::client_platform, so it reaches only the foundation, posix and libc.
reboot_add_package(os_linux/ipc
  OS LINUX
  SOURCES
    src/decimal_uid.cpp
    src/endpoint_name.cpp
    src/engine_environment.cpp
    src/engine_socket_path.cpp
    src/ipc_runtime_base.cpp
    src/messages.cpp
    src/steam_reaper.cpp
    src/systemd_unit_state.cpp
  PUBLIC_DEPS
    reboot::posix
  DEPS
    ${CMAKE_DL_LIBS})
