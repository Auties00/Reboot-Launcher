# Public headers use process's BuiltEnv, ChildRecordCallback and ChildExitInfo, and storage's DocumentStore.
reboot_add_package(gameserver
  SOURCES
    src/messages.cpp
  PUBLIC_DEPS reboot::process reboot::storage)
