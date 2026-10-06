find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(sb_unit_tests
  unit/test_btree.cpp
  unit/test_codec.cpp
  unit/test_core.cpp
  unit/test_replica.cpp
  unit/test_search.cpp
  unit/test_validation.cpp)
target_link_libraries(sb_unit_tests PRIVATE sb_core Catch2::Catch2WithMain)
target_compile_definitions(sb_unit_tests PRIVATE SB_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors")
catch_discover_tests(sb_unit_tests)

if(SB_BUILD_NET)
  add_executable(sb_integration_tests
    integration/harness.cpp
    integration/test_edge.cpp
    integration/test_cluster.cpp)
  target_link_libraries(sb_integration_tests PRIVATE sb_net Catch2::Catch2WithMain)
  target_compile_definitions(sb_integration_tests PRIVATE
    SB_EDGE_BIN="$<TARGET_FILE:sb-edge>")
  add_dependencies(sb_integration_tests sb-edge)
  catch_discover_tests(sb_integration_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
endif()
