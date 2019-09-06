// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CONCORD_CONSENSUS_HEX_TOOLS_H_
#define CONCORD_CONSENSUS_HEX_TOOLS_H_

#include <stdio.h>
#include <string>

std::ostream &hexPrint(std::ostream &s, const uint8_t *data, size_t size);

#endif  // CONCORD_CONSENSUS_HEX_TOOLS_H_
