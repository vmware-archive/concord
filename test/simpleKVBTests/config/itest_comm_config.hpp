// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef ITEST_COMM_CONFIG_HPP
#define ITEST_COMM_CONFIG_HPP

#include <string>

#include "CommDefs.hpp"
#include "Logger.hpp"
#include "ReplicaConfig.hpp"

class ITestCommConfig {
 public:
  explicit ITestCommConfig(concordlogger::Logger& logger) : logger_(logger) {}
  virtual ~ITestCommConfig() = default;

  // Create a replica config for the replica with index `replicaId`.
  // inputReplicaKeyfile is used to read the keys for this replica, and
  // default values are loaded for non-cryptographic configuration parameters.
  virtual void GetReplicaConfig(uint16_t replica_id, std::string keyFilePrefix,
                                bftEngine::ReplicaConfig* out_config) = 0;

  // Create a UDP communication configuration for the node (replica or client)
  // with index `id`.
  virtual bftEngine::PlainUdpConfig GetUDPConfig(
      bool is_replica, uint16_t id, uint16_t& num_of_clients,
      uint16_t& num_of_replicas, const std::string& config_file_name) = 0;

  // Create a UDP communication configuration for the node (replica or client)
  // with index `id`.
  virtual bftEngine::PlainTcpConfig GetTCPConfig(
      bool is_replica, uint16_t id, uint16_t& num_of_clients,
      uint16_t& num_of_replicas, const std::string& config_file_name) = 0;

  virtual bftEngine::TlsTcpConfig GetTlsTCPConfig(
      bool is_replica, uint16_t id, uint16_t& num_of_clients,
      uint16_t& num_of_replicas, const std::string& config_file_name) = 0;

 protected:
  concordlogger::Logger& logger_;
};

#endif
