# Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

include_directories(
  ${GTEST_INCLUDE_DIR}
  ${PROJECT_SOURCE_DIR}/src
  ../build/proto/
  )

message("test including " ${GTEST_LIBRARY})
message("test including " ${GTEST_MAIN_LIBRARY})

enable_testing()
add_test(NAME UtilsTests COMMAND UtilsTests)
add_test(NAME SignTests COMMAND SignTests)
add_test(NAME SliverTests COMMAND SliverTests)
add_test(NAME ConfigTests COMMAND ConfigTests)
add_test(NAME MultiIOTests COMMAND MultiIOTests)
add_test(NAME MetadataStorageTests COMMAND MetadataStorageTests)
add_test(NAME ReplicaStateSyncTests COMMAND ReplicaStateSyncTests)
add_test(NAME TimeContractTests COMMAND TimeContractTests)

add_executable(UtilsTests
  utils_test.cpp)

configure_file(resources/genesis.json resources/genesis.json COPYONLY)

target_link_libraries(UtilsTests
  ${GTEST_LIBRARIES}
  concord_utils
  gtest
  gtest_main
  pthread)

add_executable(SignTests
  signature_test.cpp)

target_link_libraries(SignTests
  ${GTEST_LIBRARIES}
  concord_utils
  gtest
  gtest_main
  pthread
  log4cplus
  secp256k1
  ${Cryptopp_LIBRARIES})

add_executable(SliverTests
  sliver_test.cpp)

target_link_libraries(SliverTests
  ${GTEST_LIBRARIES}
  concord_utils
  concord_consensus
  gtest
  gtest_main)

add_executable(ConfigTests
  config_test.cpp
  ../src/config/configuration_manager.cpp)

target_link_libraries(ConfigTests
  ${Boost_LIBRARIES}
  ${GTEST_LIBRARIES}
  ${yaml-cpp_LIBRARIES}
  concord_utils
  concord_consensus
  gtest
  gtest_main
  log4cplus
  $<TARGET_OBJECTS:logging_dev>)

add_executable(MultiIOTests
  multiIO_test.cpp)

target_link_libraries(MultiIOTests
  ${GTEST_LIBRARIES}
  concord_consensus
  log4cplus
  gtest
  gtest_main)

add_executable(MetadataStorageTests
  metadataStorage_test.cpp)

target_link_libraries(MetadataStorageTests
  ${GTEST_LIBRARIES}
  concord_consensus
  log4cplus
  gtest
  gtest_main)

add_executable(ReplicaStateSyncTests
  ../src/common/concord_log.cpp
  ../src/common/concord_types.cpp
  ../src/consensus/replica_state_sync_imp.cpp
  ../src/storage/concord_metadata_storage.cpp
  ../src/utils/concord_eth_hash.cpp
  ../src/utils/rlp.cpp
  replicaStateSync_test.cpp)

target_link_libraries(ReplicaStateSyncTests
  ${GTEST_LIBRARIES}
  log4cplus
  gtest
  gtest_main
  proto
  ${EVM_LIBRARIES}
  LLVMSupport
  concord_utils
  concord_storage)

add_executable(TimeContractTests
  time_contract_test.cpp
  ../src/config/configuration_manager.cpp)

target_link_libraries(TimeContractTests
  ${Boost_LIBRARIES}
  ${GTEST_LIBRARIES}
  ${yaml-cpp_LIBRARIES}
  log4cplus
  gtest
  gtest_main
  proto
  concord_utils
  concord_consensus
  concord_common
  concord_time
  $<TARGET_OBJECTS:logging_dev>)

add_subdirectory(simpleKVBTests)
