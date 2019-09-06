// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#include "status.hpp"

#include <ostream>

namespace concord {
namespace consensus {

std::ostream& operator<<(std::ostream& s, Status const& status) {
  return status.operator<<(s);
}

}  // namespace consensus
}  // namespace concord
