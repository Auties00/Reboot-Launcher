# Public headers expose storage's StateDocument store.
reboot_add_package(integration
  SOURCES
    src/entry_command.cpp
    src/integration_policy.cpp
    src/integration_targets.cpp
    src/messages.cpp
    src/openable_url.cpp
    src/prerequisite_spec.cpp
    src/purge_targets.cpp
  PUBLIC_DEPS reboot::storage)
