# Public headers use process's BuiltEnv, ChildRecordCallback and ChildExitInfo, and storage's DocumentStore.
reboot_add_package(gameserver
  SOURCES
    src/address_text.cpp
    src/describe_cache_document.cpp
    src/game_server_binary.cpp
    src/game_server_config.cpp
    src/game_server_description.cpp
    src/game_server_error.cpp
    src/game_server_process.cpp
    src/messages.cpp
    src/operator_command.cpp
    src/socket_role.cpp
  PUBLIC_DEPS reboot::process reboot::storage)
