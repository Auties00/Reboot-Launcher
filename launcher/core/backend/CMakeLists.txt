reboot_add_package(backend
  SOURCES
    src/account_prune_filter.cpp
    src/backend_accounts.cpp
    src/backend_config.cpp
    src/backend_event.cpp
    src/backend_launch.cpp
    src/backend_lease.cpp
    src/backend_process.cpp
    src/backend_service.cpp
    src/backend_target.cpp
    src/backend_url.cpp
    src/console_key.cpp
    src/hid_usage.cpp
    src/messages.cpp
    src/remote_backend_probe.cpp
    src/remote_login.cpp
  PUBLIC_DEPS reboot::process reboot::net reboot::identity reboot::secrets reboot::storage)
