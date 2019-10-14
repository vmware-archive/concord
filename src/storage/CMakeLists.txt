# Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

file(GLOB concord_storage_src "*.cpp" "*.hpp")

add_library(concord_storage ${concord_storage_src})

get_property(thresh_include GLOBAL PROPERTY thresh_include_folder)
target_include_directories(concord_storage PUBLIC ${thresh_include})

target_link_libraries(concord_storage PUBLIC corebft concord_consensus concordbft_storage util)