file(GLOB concord_ethereum_src "*.cpp" "*.hpp")

add_library(concord_ethereum ${concord_ethereum_src})

target_link_libraries(concord_ethereum PUBLIC
  ${EVM_LIBRARIES}
  concord_common
  concord_consensus
  LLVMSupport
  proto
  secp256k1
  ${CRYPTOPP_LIBRARIES})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/..)

# concord_ethereum currently includes time/time_signing.hpp in
# eth_kvb_commands_handler.cpp by way of time/time_contract.hpp;
# time_signing.hpp currently needs an include from bftengine.
include_directories(${bftengine_SOURCE_DIR})
