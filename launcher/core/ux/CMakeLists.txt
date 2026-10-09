reboot_add_package(ux
  SOURCES
    src/app_links.cpp
    src/doc_page.cpp
    src/help_ref.cpp
    src/language_info.cpp
    src/language_lookup.cpp
    src/language_preference.cpp
    src/language_tag.cpp
    src/links.cpp
    src/message_catalog.cpp
    src/message_catalog_export.cpp
    src/message_template.cpp
    src/messages.cpp
    src/notice.cpp
    src/notice_service.cpp
    src/onboarding.cpp
    src/onboarding_step.cpp
    src/resolve_language.cpp
    src/settings_search.cpp
    src/suggested_action.cpp
  DEPS Boost::json)

# The l10n/en.json generator and gate. It compiles every package's messages.cpp, for every OS, so its
# registry holds every message of the tree; core/testing's messages are for fakes only.
reboot_add_executable(reboot-l10n
  SOURCES tools/l10n_main.cpp
  DEPS reboot::ux)
file(GLOB reboot_l10n_messages CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/core/*/src/messages.cpp"
  "${PROJECT_SOURCE_DIR}/core-*/*/src/messages.cpp")
list(FILTER reboot_l10n_messages EXCLUDE REGEX "/core/testing/src/")
target_sources(reboot-l10n PRIVATE ${reboot_l10n_messages})
target_include_directories(reboot-l10n PRIVATE "${PROJECT_SOURCE_DIR}/core/ipc/include")
if(REBOOT_BUILD_TESTS)
  add_test(NAME l10n_catalog_complete
    COMMAND reboot-l10n check "${PROJECT_SOURCE_DIR}/l10n/en.json" "${PROJECT_SOURCE_DIR}")
  set_tests_properties(l10n_catalog_complete PROPERTIES LABELS gate)
endif()
