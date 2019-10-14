// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Commands handler interface from KVB replica

#include "commands_handler.h"

namespace concord {
namespace consensus {

// Pure virtual destructors need to be defined within an abstract class if that
// very abstract class is used to delete a derived instanciation.
// See main.cpp for its usage.
ICommandsHandler::~ICommandsHandler() = default;

}  // namespace consensus
}  // namespace concord
