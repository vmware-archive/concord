add_library(concord_utils STATIC
  "concord_utils.cpp" "concord_utils.hpp"
  "rlp.cpp" "rlp.hpp"
  "concord_eth_hash.cpp" "concord_eth_hash.hpp"
  "concord_eth_sign.cpp" "concord_eth_sign.hpp"
  "concord_prometheus_metrics.hpp" "concord_prometheus_metrics.cpp"
  "concord_logging.hpp"
)
target_link_libraries(concord_utils secp256k1 util prometheus-cpp::core
        prometheus-cpp::pull yaml-cpp ${CRYPTOPP_LIBRARIES})
target_include_directories(concord_utils PUBLIC ${CRYPTOPP_INCLUDE_DIRS})

install (FILES utils.hpp rlp.hpp concord_eth_hash.hpp concord_eth_sign.hpp
  DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
