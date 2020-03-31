string(APPEND CMAKE_CXX_FLAGS " -Wall")
if(NOT APPLE)
string(APPEND CMAKE_CXX_FLAGS " -Werror")
endif()

add_library(concmdutil STATIC
            "concmdconn.cpp" "concmdconn.hpp"
            "concmdfmt.cpp" "concmdfmt.hpp"
            "concmdopt.cpp" "concmdopt.hpp"
            "concmdex.hpp"
            ${PROTO_HDRS})
target_link_libraries(concmdutil proto)

add_executable(conc_sendtx
               "conc_sendtx.cpp"
               ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(conc_sendtx
                      concmdutil
                      protobuf::libprotobuf
                      proto ${PROTOBUF_LIBRARIES}
                      ${Boost_LIBRARIES})

add_executable(conc_gettxrcpt
               "conc_gettxrcpt.cpp"
               ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(conc_gettxrcpt
                      concmdutil
                      protobuf::libprotobuf
                      proto ${PROTOBUF_LIBRARIES}
                      ${Boost_LIBRARIES})

add_executable(conc_getstat
               "conc_getstat.cpp"
               ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(conc_getstat
                      concmdutil
                      protobuf::libprotobuf
                      proto ${PROTOBUF_LIBRARIES}
                      ${Boost_LIBRARIES})

add_executable(conc_getblk
               "conc_getblk.cpp"
               ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(conc_getblk
                      concmdutil
                      protobuf::libprotobuf
                      proto ${PROTOBUF_LIBRARIES}
                      ${Boost_LIBRARIES})

add_executable(conc_getpeers
               "conc_getpeers.cpp"
               ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(conc_getpeers
                      concmdutil
                      protobuf::libprotobuf
                      proto ${PROTOBUF_LIBRARIES}
                      ${Boost_LIBRARIES})

add_executable(conc_reconfig
               "conc_reconfig.cpp"
               ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(conc_reconfig
                      concmdutil
                      protobuf::libprotobuf
                      proto ${PROTOBUF_LIBRARIES}
                      ${Boost_LIBRARIES})

add_executable(conc_time
               "conc_time.cpp"
               ${PROJECT_SOURCE_DIR}/src/config/configuration_manager.cpp
               ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(conc_time
                      concmdutil
                      concord_consensus
                      concord_time
                      ${yaml-cpp_LIBRARIES}
                      protobuf::libprotobuf
                      proto ${PROTOBUF_LIBRARIES}
                      ${Boost_LIBRARIES}
                      $<TARGET_OBJECTS:logging_dev>)

# ecrecover and ecsign need to find src/common/concord_eth_sign.h
include_directories("../src")

add_executable(ecrecover
  "ecrecover.cpp")
target_link_libraries(ecrecover
                      concmdutil
                      concord_utils
                      log4cplus
                      secp256k1
                      ${CRYPTOPP_LIBRARIES})

add_executable(ecsign
  "ecsign.cpp")
target_link_libraries(ecsign
                      concmdutil
                      concord_utils
                      log4cplus
                      secp256k1
                      ${CRYPTOPP_LIBRARIES})

add_executable(conc_rocksdb_adp "conc_rocksdb_adp.cpp" $<TARGET_OBJECTS:logging_dev>)
target_link_libraries(conc_rocksdb_adp concord_consensus log4cplus concordbft_storage)
target_include_directories(conc_rocksdb_adp PUBLIC ${CRYPTOPP_INCLUDE_DIRS})

# conc_genconfig may also need to see "../src" to find its includes.

# Note we build conc_genconfig as statically linked so it can be run while
# setting up and deploying Concord clusters from a machine that is not a Concord
# node without having to install any additional dependencies to that machine.


add_executable(conc_genconfig
               "conc_genconfig.cpp"
               ${PROJECT_SOURCE_DIR}/src/config/configuration_manager.cpp)
set_target_properties(conc_genconfig PROPERTIES LINK_FLAGS -static)

# Note we explicitly clear conc_genconfig's LINK_LIBRARIES value here so that
# conc_genconfig will ignore any LINK_LIBRARIES appended globaly or otherwise to
# a scope containing conc_genconfig. This may be necessary as other parts of
# Concord may add LINK_LIBRARIES with a broad scope, but, given conc_genconfig's
# use of the -static link flag, its build can fail at link time if it picked up
# any shared link libraries from a broader __gmpn_add_nscopes.
set_target_properties(conc_genconfig PROPERTIES LINK_LIBRARIES "")

# To link conc_genconfig statically, we need to find separate static versions of
# certain libraries that the rest of Concord may or may not use dynamic versions
# of.
find_package(StaticLibraries REQUIRED)
target_link_libraries(conc_genconfig PUBLIC
                      ${boost_program_options_STATIC_LIBRARY}
                      ${log4cplus_STATIC_LIBRARY}
                      ${yaml-cpp_STATIC_LIBRARY}
                      $<TARGET_OBJECTS:logging_dev>)

# Note we specifically target a shared version of pthread here. We do not think
# it to significantly matter for binary correctness purposes whether pthread is
# statically or dynamically linked, as we anticipate conc_genconfig running on
# Linux systems with pthread available; however, we are explicitly using the
# shared version here as Concord may use the shared version, we do not want to
# allow a possibility for conc_genconfig to introduce licensing complications
# that Concord does not otherwise have, pthread (as a GNU/Linux system library)
# appears to be covered by GPL v2, and engineering (at the time of this writing)
# is not completely sure static linking is equivalent to dynamic linking for
# licensing and copyright purposes.
target_link_libraries(conc_genconfig PUBLIC ${pthread_SHARED_LIBRARY})

# CMake code in concord-bft/threshsign incidentally provides this convenient
# function for linking statically with the crypto libraries threshsign uses.
link_with_relic_library(conc_genconfig)

# conc_genconfig needs some cryptographic stuff from concord-bft/threshsign.
# Normally we would pick up these dependencies with either the corebft library
# or the threshsign library (defined in Concord-BFT), but these libraries do not
# seem to be defined in a way conducive to statically linking, so we have
# conc_genconfig directly target some headers and sources from the
# concord-bft/threshsign library here.
get_property(thresh_include GLOBAL PROPERTY thresh_include_folder)
target_include_directories(conc_genconfig PUBLIC ${thresh_include})
get_property(thresh_srcs TARGET threshsign PROPERTY SOURCES)
target_sources(conc_genconfig PUBLIC ${thresh_srcs})
