# Public headers use components' digest and OS types; the matrix report is JSON.
reboot_add_package(support
  SOURCES
    src/cell_inputs.cpp
    src/host_inputs.cpp
    src/messages.cpp
    src/parse_matrix_report.cpp
    src/support_policy.cpp
    src/support_reason.cpp
    src/support_verdict.cpp
    src/version_range.cpp
  DEPS Boost::json
  PUBLIC_DEPS reboot::components)
