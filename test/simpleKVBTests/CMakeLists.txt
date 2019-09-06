set(client_sources
  TesterClient/main.cpp
  basicRandomTestsRunner.cpp
  simpleKVBTestsBuilder.cpp
  tools/KeyfileIOUtils.cpp
  config/test_comm_config.cpp
  config/config_file_parser.cpp
  ${PROJECT_SOURCE_DIR}/src/config/configuration_manager.cpp)

set(replica_sources
  TesterReplica/main.cpp
  TesterReplica/internalCommandsHandler.cpp
  basicRandomTestsRunner.cpp
  simpleKVBTestsBuilder.cpp
  tools/KeyfileIOUtils.cpp
  config/test_comm_config.cpp
  config/config_file_parser.cpp
  ${PROJECT_SOURCE_DIR}/src/consensus/replica_state_sync_imp.cpp
  ${PROJECT_SOURCE_DIR}/src/common/concord_types.cpp
  ${PROJECT_SOURCE_DIR}/src/common/concord_log.cpp
  ${PROJECT_SOURCE_DIR}/src/ethereum/concord_evm.cpp
  ${PROJECT_SOURCE_DIR}/src/ethereum/evm_init_params.cpp
  ${PROJECT_SOURCE_DIR}/src/ethereum/eth_kvb_storage.cpp
  ${PROJECT_SOURCE_DIR}/src/storage/concord_metadata_storage.cpp)

set(db_editor_sources DBEditor/main.cpp)

add_executable(skvb_client ${client_sources})
add_executable(skvb_replica ${replica_sources})
add_executable(skvb_db_editor ${db_editor_sources})

target_include_directories(skvb_client PUBLIC
  ${PROJECT_SOURCE_DIR}/src config include $<TARGET_PROPERTY:logging,INTERFACE_INCLUDE_DIRECTORIES>)

target_include_directories(skvb_replica PUBLIC
  ${PROJECT_SOURCE_DIR}/src config include $<TARGET_PROPERTY:logging,INTERFACE_INCLUDE_DIRECTORIES>)

target_include_directories(skvb_db_editor PUBLIC
  ${PROJECT_SOURCE_DIR}/src include $<TARGET_PROPERTY:logging,INTERFACE_INCLUDE_DIRECTORIES>)

target_link_libraries(skvb_client
  ${yaml-cpp_LIBRARIES}
  concord_consensus
  ${Boost_LIBRARIES}
  $<TARGET_OBJECTS:logging_dev>)

target_link_libraries(skvb_replica
  proto ${PROTOBUF_LIBRARIES}
  ${EVM_LIBRARIES}
  LLVMSupport
  concord_utils
  concord_consensus
  $<TARGET_OBJECTS:logging_dev>)

target_link_libraries(skvb_db_editor
  concord_common
  concord_consensus)

add_custom_target(copy_blockchain_scripts3 ALL COMMENT "Copying scripts abcd")

add_custom_command(TARGET copy_blockchain_scripts3
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/scripts
  ${CMAKE_CURRENT_BINARY_DIR}/scripts)

