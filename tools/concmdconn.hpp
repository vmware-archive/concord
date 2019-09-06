// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// concord connection for command line tools.

#ifndef CONCMDCONN_HPP
#define CONCMDCONN_HPP

#include <boost/program_options.hpp>
#include "concord.pb.h"

bool call_concord(boost::program_options::variables_map &opts,
                  com::vmware::concord::ConcordRequest &request,
                  com::vmware::concord::ConcordResponse &response /* out */);

#endif
