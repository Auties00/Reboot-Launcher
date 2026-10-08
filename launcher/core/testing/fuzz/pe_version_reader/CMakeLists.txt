if(NOT TARGET reboot_builds)
  return()
endif()
reboot_add_fuzz_target(reboot-fuzz-pe-version-reader CORPUS pe_version_reader
  SOURCES pe_version_reader_fuzz.cpp
  DEPS reboot::builds)
