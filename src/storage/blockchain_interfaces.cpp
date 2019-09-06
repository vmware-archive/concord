// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "blockchain_interfaces.h"

namespace concord {
namespace storage {

// Pure virtual destructors need to be defined within an abstract class if that
// very abstract class is used to delete a derived instanciation.
// See main.cpp for its usage.
ICommandsHandler::~ICommandsHandler() = default;

}  // namespace storage
}  // namespace concord
