# Public headers name storage's DocumentStore, whose Document constraint needs the complete documents.
reboot_add_package(updates
  SOURCES
    src/apply_gate.cpp
    src/messages.cpp
    src/pending_update_marker.cpp
    src/resume_record.cpp
    src/update_event.cpp
    src/update_offer.cpp
  DEPS reboot::net Boost::json
  PUBLIC_DEPS reboot::components reboot::storage)
