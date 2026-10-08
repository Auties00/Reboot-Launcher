# Shared POSIX adapters; linked only by core-macos and core-linux.
reboot_add_package(posix
  OS POSIX
  SOURCES
    src/ignore_sigpipe.cpp
    src/messages.cpp
    src/owner_only_directory.cpp
    src/peer_credential_check.cpp
    src/posix_error.cpp
    src/posix_file_system.cpp
    # Only the macOS launcher spawns through PosixSpawner.
    $<$<PLATFORM_ID:Darwin>:src/posix_spawner.cpp>
    src/socket_fds.cpp
    src/socket_stream.cpp
    src/unique_fd.cpp
    src/unix_endpoint_checks.cpp
    src/unix_socket_connector_base.cpp
    src/unix_socket_listener_base.cpp)
