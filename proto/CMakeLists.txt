# Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

# The base path of the concord-communcation module, where we're keeping
# shared resources for services that communicate with concord nodes.
set(COMMUNICATION_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../communication")

set(PROTOBUF_IMPORT_DIRS ${COMMUNICATION_PATH}/src/main/proto/)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
  ${CMAKE_CURRENT_BINARY_DIR}
  ${COMMUNICATION_PATH}/src/main/proto/concord.proto
  concord_storage.proto
)

add_library(proto STATIC ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(proto protobuf::libprotobuf)
target_include_directories(proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

