# Public headers use gameserver, sessions, publish, net, support, storage and process types;
# HostServiceDeps names process::ChildRecordCallback.
reboot_add_package(host
  SOURCES
    src/host_error.cpp
    src/host_profile.cpp
    src/host_start_request.cpp
    src/ip_cidr.cpp
    src/match_end_policy.cpp
    src/messages.cpp
    src/operator_policy.cpp
    src/port_block.cpp
    src/port_policy.cpp
  DEPS reboot::builds reboot::identity
  PUBLIC_DEPS reboot::storage reboot::support reboot::net reboot::gameserver reboot::sessions reboot::publish
              reboot::process)
