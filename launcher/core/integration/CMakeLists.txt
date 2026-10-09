# Public headers expose storage's StateDocument store.
reboot_add_package(integration
  SOURCES
    src/entry_command.cpp
    src/entry_error.cpp
    src/entry_ops.cpp
    src/install_integration.cpp
    src/integration_policy.cpp
    src/integration_service.cpp
    src/integration_targets.cpp
    src/messages.cpp
    src/openable_url.cpp
    src/prerequisite_error.cpp
    src/prerequisite_service.cpp
    src/prerequisite_spec.cpp
    src/purge_error.cpp
    src/purge_service.cpp
    src/purge_targets.cpp
    src/shell_error.cpp
    src/shell_service.cpp
    src/uninstall_integration.cpp
  PUBLIC_DEPS reboot::storage)
