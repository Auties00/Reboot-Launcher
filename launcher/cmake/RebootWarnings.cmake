include_guard(GLOBAL)

# The server-browser warning set, with warnings as errors. Packages link it privately.
add_library(reboot_warnings INTERFACE)
if(MSVC)
  target_compile_options(reboot_warnings INTERFACE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
else()
  target_compile_options(reboot_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    -Wno-sign-conversion -Wno-missing-field-initializers -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
    -Wimplicit-fallthrough)
endif()
