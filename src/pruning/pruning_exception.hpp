// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CONCORD_PRUNING_PRUNING_EXCEPTION_HPP
#define CONCORD_PRUNING_PRUNING_EXCEPTION_HPP

#include <exception>
#include <string>

namespace concord {
namespace pruning {

// Base class for pruning-related exceptions.
class PruningException : public std::exception {
 public:
  explicit PruningException(const std::string& what) : msg_{what} {}

  const char* what() const noexcept override { return msg_.c_str(); }

 private:
  std::string msg_;
};

class PruningConfigurationException : public PruningException {
 public:
  explicit PruningConfigurationException(const std::string& what)
      : PruningException{what} {}
};

class PruningRuntimeException : public PruningException {
 public:
  explicit PruningRuntimeException(const std::string& what)
      : PruningException{what} {}
};

}  // namespace pruning
}  // namespace concord

#endif  //  CONCORD_PRUNING_PRUNING_EXCEPTION_HPP
