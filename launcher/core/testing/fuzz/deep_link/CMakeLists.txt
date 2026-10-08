if(NOT TARGET reboot_browser)
  return()
endif()
reboot_add_fuzz_target(reboot-fuzz-deep-link CORPUS deep_link SOURCES deep_link_fuzz.cpp DEPS reboot::browser)
