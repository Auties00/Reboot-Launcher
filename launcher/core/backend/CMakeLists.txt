reboot_add_package(backend
  SOURCES
    src/account_prune_filter.cpp
    src/backend_config.cpp
    src/backend_target.cpp
    src/backend_url.cpp
    src/console_key.cpp
    src/hid_usage.cpp
    src/messages.cpp
  PUBLIC_DEPS reboot::process reboot::net reboot::identity reboot::secrets reboot::storage)
