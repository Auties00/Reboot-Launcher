# Public headers use storage's HostListing and AccountRecord, and browser's RbsbEndpoint.
reboot_add_package(publish
  SOURCES
    src/field_checks.cpp
    src/host_metadata.cpp
    src/messages.cpp
    src/metadata_patch.cpp
    src/publish_request.cpp
    src/rejected_field.cpp
  PUBLIC_DEPS reboot::storage reboot::browser)
