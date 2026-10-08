if(NOT TARGET reboot_process)
  return()
endif()
reboot_add_fuzz_target(reboot-fuzz-line-reader CORPUS line_reader SOURCES line_reader_fuzz.cpp DEPS reboot::process)
