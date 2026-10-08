reboot_add_package(components
  SOURCES
    src/messages.cpp
  DEPS reboot::trust reboot::net reboot::storage)

# Runtime archives are unpacked in process.
target_link_libraries(reboot_components PRIVATE LibArchive::LibArchive)
