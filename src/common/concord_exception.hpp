// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Exceptions used in Concord

#ifndef COMMON_CONCORD_EXCEPTION_HPP
#define COMMON_CONCORD_EXCEPTION_HPP

#include <exception>
#include <string>

namespace concord {
namespace common {

class EVMException : public std::exception {
 public:
  explicit EVMException(const std::string& what) : msg(what){};

  virtual const char* what() const noexcept override { return msg.c_str(); }

 private:
  std::string msg;
};

class ReadOnlyModeException : public EVMException {
 public:
  ReadOnlyModeException()
      : EVMException("Write attempted on read-only storage") {}
};

class TransactionNotFoundException : public EVMException {
 public:
  TransactionNotFoundException() : EVMException("Transaction not found") {}
};

class BlockNotFoundException : public EVMException {
 public:
  BlockNotFoundException() : EVMException("Block not found") {}
};

class AccountNotFoundException : public EVMException {
 public:
  AccountNotFoundException() : EVMException("Account or contract not found") {}
};

}  // namespace common
}  // namespace concord

#endif  // COMMON_CONCORD_EXCEPTION_HPP
