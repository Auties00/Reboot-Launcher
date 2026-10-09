# Engine IPC transport (AF_UNIX) and the client-side ports; linked by reboot-engine and by
# reboot_client through reboot::client_platform, so it reaches only the foundation, posix and libc.
reboot_add_package(os_linux/ipc
  OS LINUX
  SOURCES
    src/caller_facts.cpp
    src/client_path_rules.cpp
    src/decimal_uid.cpp
    src/detached_spawn.cpp
    src/endpoint_name.cpp
    src/engine_endpoint.cpp
    src/engine_environment.cpp
    src/engine_socket_path.cpp
    src/engine_start_plan.cpp
    src/ipc_runtime_base.cpp
    src/linux_caller_context.cpp
    src/linux_client_paths.cpp
    src/linux_peer_credentials.cpp
    src/make_client_platform.cpp
    src/messages.cpp
    src/runtime_base_check.cpp
    src/socket_activation.cpp
    src/state_locks.cpp
    src/steam_reaper.cpp
    src/systemd_engine_starter.cpp
    src/systemd_unit_state.cpp
    src/tool_run.cpp
    src/unix_socket_connector.cpp
    src/unix_socket_listener.cpp
  PUBLIC_DEPS
    reboot::posix
  DEPS
    ${CMAKE_DL_LIBS})
