// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Formatting utilities for command line tools.

#ifndef CONCMDFMT_HPP
#define CONCMDFMT_HPP

#include <string>

void dehex0x(const std::string &str, std::string &bin /* out */);
void hex0x(const std::string &in, std::string &out /* out */);

#endif
