# Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

file(GLOB concord_consensus_src "*.cpp" "*.hpp" "*.h")

add_library(concord_consensus ${concord_consensus_src})

get_property(thresh_include GLOBAL PROPERTY thresh_include_folder)
target_include_directories(concord_consensus PUBLIC ${thresh_include} $<TARGET_PROPERTY:logging,INTERFACE_INCLUDE_DIRECTORIES>)

target_link_libraries(concord_consensus PUBLIC corebft concord_storage proto concord_time hdr_histogram_static util)

if(USE_ROCKSDB)
find_library(ROCKSDB rocksdb)
find_library(LIBBZ2 bz2)
find_library(LIBLZ4 lz4)
find_library(LIBZSTD zstd)
find_library(LIBZ z)
find_library(LIBSNAPPY snappy)
target_compile_definitions(concord_consensus PRIVATE USE_ROCKSDB=1)
target_link_libraries(concord_consensus PUBLIC ${ROCKSDB} ${LIBBZ2} ${LIBLZ4} ${LIBZSTD} ${LIBZ} ${LIBSNAPPY})
endif()

target_compile_definitions(concord_consensus PUBLIC __BASE=1 SPARSE_STATE=1 USE_TLS=1)
if(${BUILD_COMM_TCP_TLS})
    target_compile_definitions(concord_consensus PUBLIC USE_COMM_TLS_TCP)
endif()
