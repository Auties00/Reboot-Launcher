# sb_core: portable logic (wire, registry, search, security, inproc backbone). No MsQuic.
add_library(sb_core STATIC
  backbone/inproc.cpp
  ops/log.cpp
  registry/replica.cpp
  registry/search.cpp
  registry/search_worker.cpp
  security/crypto.cpp
  security/selfsigned.cpp)
target_include_directories(sb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sb_core
  PUBLIC sb_options Boost::headers Boost::pfr Boost::container Boost::unordered roaring::roaring
         $<IF:$<TARGET_EXISTS:zstd::libzstd>,zstd::libzstd,$<IF:$<TARGET_EXISTS:zstd::libzstd_static>,zstd::libzstd_static,zstd::libzstd_shared>>
         OpenSSL::Crypto Threads::Threads
  PRIVATE sb_warnings)

if(SB_BUILD_NET)
  find_package(maxminddb CONFIG QUIET)
  add_library(sb_net STATIC
    backbone/factory.cpp
    client/client.cpp
    edge/config.cpp
    edge/edge.cpp
    edge/geoip.cpp
    edge/prober.cpp
    edge/shard.cpp
    ops/admin_http.cpp
    ops/sd_notify.cpp)
  if(SB_WITH_NATS)
    target_sources(sb_net PRIVATE backbone/nats.cpp)
    target_link_libraries(sb_net PUBLIC $<IF:$<TARGET_EXISTS:cnats::nats_static>,cnats::nats_static,cnats::nats>)
  endif()
  target_compile_definitions(sb_net PUBLIC SB_WITH_NATS=$<BOOL:${SB_WITH_NATS}>)
  target_link_libraries(sb_net PUBLIC sb_core MsQuic::MsQuic tomlplusplus::tomlplusplus PRIVATE sb_warnings)
  if(maxminddb_FOUND)
    target_link_libraries(sb_net PRIVATE maxminddb::maxminddb)
  endif()
endif()
