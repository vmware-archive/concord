// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Concord node startup.

#include <log4cplus/configurator.h>
#include <log4cplus/loggingmacros.h>
#include <boost/program_options.hpp>
#include <boost/thread.hpp>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include "api/api_acceptor.hpp"
#include "blockchain/db_adapter.h"
#include "blockchain/db_interfaces.h"
#include "common/concord_exception.hpp"
#include "common/status_aggregator.hpp"
#include "config/configuration_manager.hpp"
#include "consensus/bft_configuration.hpp"
#include "consensus/client_imp.h"
#include "consensus/replica_imp.h"
#include "consensus/replica_state_sync_imp.hpp"
#include "ethereum/concord_evm.hpp"
#include "ethereum/eth_kvb_commands_handler.hpp"
#include "ethereum/eth_kvb_storage.hpp"
#include "ethereum/evm_init_params.hpp"
#include "memorydb/client.h"
#include "memorydb/key_comparator.h"
#include "rocksdb/client.h"
#include "rocksdb/key_comparator.h"
#include "time/time_pusher.hpp"
#include "time/time_reading.hpp"
#include "utils/concord_eth_sign.hpp"

using namespace boost::program_options;
using namespace std;

using boost::asio::io_service;
using boost::asio::ip::address;
using boost::asio::ip::tcp;
using log4cplus::Logger;

using concord::api::ApiAcceptor;
using concord::common::EthLog;
using concord::common::EthTransaction;
using concord::common::EVMException;
using concord::common::StatusAggregator;
using concord::common::zero_address;
using concord::common::zero_hash;
using concord::config::ConcordConfiguration;
using concord::ethereum::EthKvbStorage;
using concord::common::operator<<;
using concord::consensus::ClientConsensusConfig;
using concord::consensus::CommConfig;
using concord::consensus::IClient;
using concord::consensus::ICommandsHandler;
using concord::consensus::IReplica;
using concord::consensus::KVBClient;
using concord::consensus::KVBClientPool;
using concord::consensus::ReplicaConsensusConfig;
using concord::consensus::ReplicaImp;
using concord::ethereum::EthKvbCommandsHandler;
using concord::ethereum::EVM;
using concord::ethereum::EVMInitParams;
using concord::storage::IDBClient;
using concord::storage::blockchain::DBAdapter;
using concord::storage::blockchain::IBlocksAppender;
using concord::storage::blockchain::ILocalKeyValueStorageReadOnly;
using concord::storage::blockchain::KeyManipulator;
using concordUtils::BlockId;
using concordUtils::SetOfKeyValuePairs;
using concordUtils::Status;

using concord::time::TimePusher;
using concord::utils::EthSign;

// Parse BFT configuration
using concord::consensus::initializeSBFTConfiguration;

// the Boost service hosting our Helen connections
static io_service *api_service = nullptr;
static boost::thread_group worker_pool;
// 50 MiBytes
void signalHandler(int signum) {
  try {
    Logger logger = Logger::getInstance("com.vmware.concord.main");
    LOG4CPLUS_INFO(logger, "Signal received (" << signum << ")");

    if (api_service) {
      LOG4CPLUS_INFO(logger, "Stopping API service");
      api_service->stop();
    }
  } catch (exception &e) {
    cout << "Exception in signal handler: " << e.what() << endl;
  }
}

IDBClient *open_database(ConcordConfiguration &nodeConfig, Logger logger) {
  if (!nodeConfig.hasValue<std::string>("blockchain_db_impl")) {
    LOG4CPLUS_FATAL(logger, "Missing blockchain_db_impl config");
    throw EVMException("Missing blockchain_db_impl config");
  }

  string db_impl_name = nodeConfig.getValue<std::string>("blockchain_db_impl");
  if (db_impl_name == "memory") {
    LOG4CPLUS_INFO(logger, "Using memory blockchain database");
    // Client makes a copy of comparator, so scope lifetime is not a problem
    // here.
    concord::storage::memorydb::KeyComparator comparator(new KeyManipulator);
    return new concord::storage::memorydb::Client(comparator);
#ifdef USE_ROCKSDB
  } else if (db_impl_name == "rocksdb") {
    LOG4CPLUS_INFO(logger, "Using rocksdb blockchain database");
    string rocks_path = nodeConfig.getValue<std::string>("blockchain_db_path");
    return new concord::storage::rocksdb::Client(
        rocks_path,
        new concord::storage::rocksdb::KeyComparator(new KeyManipulator()));
#endif
  } else {
    LOG4CPLUS_FATAL(logger, "Unknown blockchain_db_impl " << db_impl_name);
    throw EVMException("Unknown blockchain_db_impl");
  }
}

/**
 * IdleBlockAppender is a shim to wrap IReplica::addBlocktoIdleReplica in an
 * IBlocksAppender interface, so that it can be rewrapped in a EthKvbStorage
 * object, thus allowing the create_genesis_block function to use the same
 * functions as concord_evm to put data in the genesis block.
 */
class IdleBlockAppender : public IBlocksAppender {
 private:
  IReplica *replica_;

 public:
  IdleBlockAppender(IReplica *replica) : replica_(replica) {}

  concordUtils::Status addBlock(const SetOfKeyValuePairs &updates,
                                BlockId &outBlockId) override {
    outBlockId = 0;  // genesis only!
    return replica_->addBlockToIdleReplica(updates);
  }
};

/**
 * Create the initial transactions and a genesis block based on the
 * genesis file.
 */
concordUtils::Status create_genesis_block(IReplica *replica,
                                          EVMInitParams params, Logger logger) {
  const ILocalKeyValueStorageReadOnly &storage = replica->getReadOnlyStorage();
  IdleBlockAppender blockAppender(replica);
  EthKvbStorage kvbStorage(storage, &blockAppender);

  if (storage.getLastBlock() > 0) {
    LOG4CPLUS_INFO(logger, "Blocks already loaded, skipping genesis");
    return concordUtils::Status::OK();
  }

  std::map<evmc_address, evmc_uint256be> genesis_acts =
      params.get_initial_accounts();
  uint64_t nonce = 0;
  uint64_t chainID = params.get_chainID();
  for (auto it = genesis_acts.begin(); it != genesis_acts.end(); ++it) {
    // store a transaction for each initial balance in the genesis block
    // defintition
    EthTransaction tx = {
        nonce,                   // nonce
        zero_hash,               // block_hash: will be set in write_block
        0,                       // block_number
        zero_address,            // from
        it->first,               // to
        zero_address,            // contract_address
        std::vector<uint8_t>(),  // input
        EVMC_SUCCESS,            // status
        it->second,              // value
        0,                       // gas_price
        0,                       // gas_limit
        0,                       // gas_used
        std::vector<concord::common::EthLog>(),  // logs
        zero_hash,          // sig_r (no signature for genesis)
        zero_hash,          // sig_s (no signature for genesis)
        (chainID * 2 + 35)  // sig_v
    };
    evmc_uint256be txhash = tx.hash();
    LOG4CPLUS_INFO(logger, "Created genesis transaction "
                               << txhash << " to address " << it->first
                               << " with value = " << tx.value);
    kvbStorage.add_transaction(tx);

    // also set the balance record
    kvbStorage.set_balance(it->first, it->second);
    nonce++;
  }
  kvbStorage.set_nonce(zero_address, nonce);

  uint64_t timestamp = params.get_timestamp();
  uint64_t gasLimit = params.get_gas_limit();

  // Genesis is always proposed and accepted at the same time.
  return kvbStorage.write_block(timestamp, gasLimit);
}

/*
 * Starts a set of worker threads which will call api_service.run() method.
 * This will allow us to have multiple threads accepting tcp connections
 * and passing the requests to KVBClient.
 */
void start_worker_threads(int number) {
  Logger logger = Logger::getInstance("com.vmware.concord.main");
  LOG4CPLUS_INFO(logger, "Starting " << number << " new API worker threads");
  assert(api_service);
  for (int i = 0; i < number; i++) {
    boost::thread *t = new boost::thread(
        boost::bind(&boost::asio::io_service::run, api_service));
    worker_pool.add_thread(t);
  }
}

/*
 * Start the service that listens for connections from Helen.
 */
int run_service(ConcordConfiguration &config, ConcordConfiguration &nodeConfig,
                Logger &logger) {
  unique_ptr<EVM> concevm;
  unique_ptr<EthSign> ethVerifier;
  EVMInitParams params;
  uint64_t chainID;

  try {
    if (nodeConfig.hasValue<std::string>("genesis_block")) {
      string genesis_file_path =
          nodeConfig.getValue<std::string>("genesis_block");
      LOG4CPLUS_INFO(logger,
                     "Reading genesis block from " << genesis_file_path);
      params = EVMInitParams(genesis_file_path);
      chainID = params.get_chainID();
      // throws an exception if it fails
      concevm = unique_ptr<EVM>(new EVM(params));
      ethVerifier = unique_ptr<EthSign>(new EthSign());
    } else {
      LOG4CPLUS_WARN(logger, "No genesis block provided");
    }

    // Replica and communication config
    CommConfig commConfig;
    StatusAggregator sag;
    commConfig.statusCallback = sag.get_update_connectivity_fn();
    ReplicaConsensusConfig replicaConsensusConfig;

    // TODO(IG): check return value and shutdown concord if false
    initializeSBFTConfiguration(config, nodeConfig, &commConfig, nullptr, 0,
                                &replicaConsensusConfig);

    DBAdapter db_adapter(open_database(nodeConfig, logger));

    // Replica
    //
    // TODO(IG): since ReplicaImpl is used as an implementation of few
    // intefaces, this object will be used for constructing
    // EthKvbCommandsHandler and thus we cant use IReplica here. Need to
    // restructure the code, to split interfaces implementation and to construct
    // objects in more clear way
    concord::consensus::ReplicaStateSyncImp replicaStateSync;
    ReplicaImp replica(commConfig, replicaConsensusConfig, &db_adapter,
                       replicaStateSync);

    unique_ptr<ICommandsHandler> kvb_commands_handler;

    kvb_commands_handler =
        unique_ptr<ICommandsHandler>(new EthKvbCommandsHandler(
            *concevm, *ethVerifier, config, nodeConfig, replica, replica));
    // Genesis must be added before the replica is started.
    concordUtils::Status genesis_status =
        create_genesis_block(&replica, params, logger);
    if (!genesis_status.isOK()) {
      LOG4CPLUS_FATAL(logger,
                      "Unable to load genesis block: " << genesis_status);
      throw EVMException("Unable to load genesis block");
    }

    replica.set_command_handler(kvb_commands_handler.get());
    replica.start();

    // Clients

    std::shared_ptr<TimePusher> timePusher;
    if (concord::time::IsTimeServiceEnabled(config)) {
      timePusher.reset(new TimePusher(config, nodeConfig));
    }

    std::vector<KVBClient *> clients;

    bool client_timing_enabled = config.getValue<bool>("client_timing_enabled");
    std::chrono::steady_clock::duration client_timing_log_period =
        std::chrono::seconds(
            client_timing_enabled
                ? config.getValue<uint32_t>("client_timing_log_period_sec")
                : 0);
    std::chrono::milliseconds clientTimeout(
        nodeConfig.getValue<uint32_t>("bft_client_timeout_ms"));
    for (uint16_t i = 0;
         i < config.getValue<uint16_t>("client_proxies_per_replica"); ++i) {
      ClientConsensusConfig clientConsensusConfig;
      // TODO(IG): check return value and shutdown concord if false
      CommConfig clientCommConfig;
      initializeSBFTConfiguration(config, nodeConfig, &clientCommConfig,
                                  &clientConsensusConfig, i, nullptr);

      IClient *client = concord::consensus::createClient(clientCommConfig,
                                                         clientConsensusConfig);
      client->start();
      KVBClient *kvbClient =
          new KVBClient(client, clientTimeout, timePusher,
                        client_timing_enabled, client_timing_log_period,
                        std::to_string(clientConsensusConfig.clientId));
      clients.push_back(kvbClient);
    }

    KVBClientPool pool(clients, timePusher);

    if (timePusher) {
      timePusher->Start(&pool);
    }

    signal(SIGINT, signalHandler);
    signal(SIGABRT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Start the Management API service. If Ethereum is enabled we also expose
    // Ethereum API through this service.
    std::string ip = nodeConfig.getValue<std::string>("service_host");
    short port = nodeConfig.getValue<short>("service_port");

    api_service = new io_service();
    tcp::endpoint endpoint(address::from_string(ip), port);
    uint64_t gasLimit = config.getValue<uint64_t>("gas_limit");
    ApiAcceptor acceptor(*api_service, endpoint, pool, sag, gasLimit, chainID,
                         true);
    LOG4CPLUS_INFO(logger, "API Listening on " << endpoint);

    start_worker_threads(nodeConfig.getValue<int>("api_worker_pool_size") - 1);

    // Wait for api_service->run() to return
    api_service->run();
    worker_pool.join_all();

    if (timePusher) {
      timePusher->Stop();
    }

    replica.stop();
  } catch (std::exception &ex) {
    LOG4CPLUS_FATAL(logger, ex.what());
    return -1;
  }

  return 0;
}

int main(int argc, char **argv) {
  bool loggerInitialized = false;
  int result = 0;

  try {
    ConcordConfiguration config;

    // We initialize the logger to whatever log4cplus defaults to here so that
    // issues that arise while loading the configuration can be logged; the
    // log4cplus::ConfigureAndWatchThread for using and updating the requested
    // logger configuration file will be created once the configuration has been
    // loaded and we can read the path for this file from it.
    log4cplus::initialize();
    log4cplus::BasicConfigurator loggerInitConfig;
    loggerInitConfig.configure();

    // Note that this must be the very first statement
    // in main function before doing any operations on config
    // parameters or 'argc/argv'. Never directly operate on
    // config parameters or command line parameters directly
    // always use po::variables_map interface for that.
    variables_map opts = initialize_config(config, argc, argv);

    if (opts.count("help")) return result;

    if (opts.count("debug")) std::this_thread::sleep_for(chrono::seconds(20));

    // Get a reference to the node instance-specific configuration for the
    // current running Concord node because that is needed frequently and we do
    // not want to have to determine the current node every time.
    size_t nodeIndex = detectLocalNode(config);
    ConcordConfiguration &nodeConfig = config.subscope("node", nodeIndex);

    // Initialize logger
    log4cplus::ConfigureAndWatchThread configureThread(
        nodeConfig.getValue<std::string>("logger_config"),
        nodeConfig.getValue<int>("logger_reconfig_time"));
    loggerInitialized = true;

    // say hello
    Logger mainLogger = Logger::getInstance("com.vmware.concord.main");
    LOG4CPLUS_INFO(mainLogger, "VMware Project concord starting");

    // actually run the service - when this call returns, the
    // service has shutdown
    result = run_service(config, nodeConfig, mainLogger);

    LOG4CPLUS_INFO(mainLogger, "VMware Project concord halting");
  } catch (const error &ex) {
    if (loggerInitialized) {
      Logger mainLogger = Logger::getInstance("com.vmware.concord.main");
      LOG4CPLUS_FATAL(mainLogger, ex.what());
    } else {
      std::cerr << ex.what() << std::endl;
    }
    result = -1;
  }

  if (loggerInitialized) {
    Logger mainLogger = Logger::getInstance("com.vmware.concord.main");
    LOG4CPLUS_INFO(mainLogger, "Shutting down");
  }

  // cleanup required for properties-watching thread
  log4cplus::Logger::shutdown();

  return result;
}