# Public headers use storage's SettingsSnapshot and implement builds' IBuildUsage.
reboot_add_package(sessions
  SOURCES
    src/messages.cpp
    src/session_registry.cpp
    src/sessions_error.cpp
    src/shutdown_coordinator.cpp
  PUBLIC_DEPS reboot::storage reboot::builds)
