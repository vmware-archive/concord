file(GLOB concord_src "main.cpp"
  "api/*.cpp"        "api/*.hpp"
  "config/*.cpp"     "config/*.hpp"
)

string(APPEND CMAKE_CXX_FLAGS " -Wall")
if(NOT APPLE)
string(APPEND CMAKE_CXX_FLAGS " -Werror")
endif()

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory("common")
add_subdirectory("consensus")
add_subdirectory("ethereum")
add_subdirectory("storage")
add_subdirectory("time")
add_subdirectory("utils")

add_executable(concord
  ${concord_src}
  ${PROTO_SRCS} ${PROTO_HDRS}
  ${GRPC_SRCS}
)

if(USE_ROCKSDB)
target_compile_definitions(concord PRIVATE USE_ROCKSDB=1)
endif()

target_compile_definitions(concord PRIVATE __BASE=1 SPARSE_STATE=1 USE_TLS=1)

target_link_libraries(concord
  proto
  ${Boost_LIBRARIES}
  log4cplus
  ${yaml-cpp_LIBRARIES}
  concord_common
  concord_consensus
  concord_ethereum
  concord_storage
  concord_time
  concord_utils
  $<TARGET_OBJECTS:logging_dev>
)
