// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Options for command line tools.

#include "concmdopt.hpp"
#include <iostream>

using boost::program_options::value;

/**
 * Parse command line options. Options that are common to all tools are added
 * (help, address, and port) automatically. Pass a function as "adder" to add
 * tool-specific options. The function will be passed an options_description,
 * to which it should add its specific options.
 *
 * If "help" is in the given arguments, this function prints help and the
 * returns false. Otherwise, options are notified (see boost program_options)
 * and this function returns true.
 */
bool parse_options(int argc, char **argv, options_adder adder,
                   boost::program_options::variables_map &opts) {
  boost::program_options::options_description desc{"Options"};

  // clang-format off
  desc.add_options()
    (OPT_HELP ",h", "Print this help message")
    (OPT_ADDRESS ",a", value<std::string>()->default_value(DEFAULT_CONCORD_IP),
     "IP address of concord node")
    (OPT_PORT ",p", value<std::string>()->default_value(DEFAULT_CONCORD_PORT),
     "Port of concord node")
    (OPT_FORMAT ",o", value<std::string>()->default_value(DEFAULT_FORMAT),
     "Output format. \"" OPT_FORMAT_TEXT "\" (default) or \""
     OPT_FORMAT_JSON "\"");
  // clang-format on

  // add tool-specific options
  if (adder != NULL) {
    (*adder)(desc);
  }

  store(parse_command_line(argc, argv, desc), opts);

  if (opts.count(OPT_HELP)) {
    std::cout << desc << std::endl;
    // help has been printed, program should not continue
    return false;
  }

  // After help-check, so that required params are not required for help.
  notify(opts);

  if (opts[OPT_FORMAT].as<std::string>() != OPT_FORMAT_TEXT &&
      opts[OPT_FORMAT].as<std::string>() != OPT_FORMAT_JSON) {
    std::cerr << "Unknown output format \""
              << opts[OPT_FORMAT].as<std::string>() << "\"" << std::endl;
    return false;
  }

  // help was not requested, program should continue
  return true;
}
