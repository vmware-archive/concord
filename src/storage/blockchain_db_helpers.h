// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
#ifndef CONCORD_STORAGE_BLOCKCHAIN_DB_HELPERS_H_
#define CONCORD_STORAGE_BLOCKCHAIN_DB_HELPERS_H_

#include <cstdint>
#include <cstring>

namespace concord {
namespace storage {

bool copyToAndAdvance(uint8_t *_buf, size_t *_offset, size_t _maxOffset,
                      uint8_t *_src, size_t _srcSize);

}  // namespace storage
}  // namespace concord

#endif  // CONCORD_STORAGE_BLOCKCHAIN_DB_HELPERS_H_
