add_library(sb_warnings INTERFACE)
target_compile_options(sb_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
  -Wno-missing-field-initializers -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wimplicit-fallthrough
  $<$<CONFIG:Release,RelWithDebInfo>:-fno-plt>)
