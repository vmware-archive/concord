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
                      ${Cryptopp_LIBRARIES})

add_executable(ecsign
  "ecsign.cpp")
target_link_libraries(ecsign
                      concmdutil
                      concord_utils
                      log4cplus
                      secp256k1
                      ${Cryptopp_LIBRARIES})

add_executable(conc_rocksdb_adp "conc_rocksdb_adp.cpp")
target_link_libraries(conc_rocksdb_adp concord_consensus log4cplus concordbft_storage)
target_include_directories(conc_rocksdb_adp PUBLIC ${Cryptopp_INCLUDE_DIRS})

# conc_genconfig also needs to see "../src" to find its includes.
add_executable(conc_genconfig
               "conc_genconfig.cpp"
               ${PROJECT_SOURCE_DIR}/src/config/configuration_manager.cpp)
target_link_libraries(conc_genconfig
                      ${Boost_LIBRARIES}
                      ${yaml-cpp_LIBRARIES}
                      concord_consensus
                      log4cplus
                      $<TARGET_OBJECTS:logging_dev>)
