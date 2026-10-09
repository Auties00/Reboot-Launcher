# Public headers use storage's HostListing and AccountRecord, and browser's RbsbEndpoint.
reboot_add_package(publish
  SOURCES
    src/field_checks.cpp
    src/host_identity_store.cpp
    src/host_metadata.cpp
    src/host_publisher.cpp
    src/identity_file.cpp
    src/messages.cpp
    src/metadata_patch.cpp
    src/publish_request.cpp
    src/rejected_field.cpp
  PUBLIC_DEPS reboot::storage reboot::browser)
