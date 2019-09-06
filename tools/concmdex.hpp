// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Exceptions for command line tools.

#ifndef CONCMDEX_HPP
#define CONCMDEX_HPP

class ConcCmdException : public std::exception {
 public:
  explicit ConcCmdException(const std::string& what) : msg(what){};

  virtual const char* what() const noexcept override { return msg.c_str(); }

 private:
  std::string msg;
};

#endif
