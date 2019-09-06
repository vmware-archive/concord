// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
// This module provides an ability for offline DB modifications for a specific
// replica.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>
#include "commonKVBTests.hpp"

#define USE_ROCKSDB 1

#ifdef USE_LOG4CPP
#include <log4cplus/configurator.h>
#endif

#include "Logger.hpp"
#include "storage/comparators.h"
#include "storage/rocksdb_client.h"
#include "storage/rocksdb_metadata_storage.h"

using namespace bftEngine;
using namespace std;

using concord::consensus::Status;
using concord::storage::ObjectIdsVector;
using concord::storage::RocksDBClient;
using concord::storage::RocksDBMetadataStorage;
using concord::storage::RocksKeyComparator;

stringstream dbPath;
RocksDBClient *dbClient = nullptr;
const uint32_t MAX_OBJECT_ID = 10;
const uint32_t MAX_OBJECT_SIZE = 200;
int numOfObjectsToAdd = MAX_OBJECT_ID;
const uint32_t firstObjId = 2;
RocksDBMetadataStorage *metadataStorage = nullptr;
ObjectIdsVector objectIdsVector;

enum DB_OPERATION {
  NO_OPERATION,
  GET_LAST_BLOCK_SEQ_NBR,
  ADD_STATE_METADATA_OBJECTS,
  DELETE_LAST_STATE_METADATA,
  DUMP_ALL_VALUES
};

DB_OPERATION dbOperation = NO_OPERATION;

auto logger = concordlogger::Log::getLogger("skvbtest.db_editor");

void printUsageAndExit(char **argv) {
  LOG_ERROR(logger, "Wrong usage! \nRequired parameters: "
                        << argv[0] << " -p FULL_PATH_TO_DB_DIR \n"
                        << "One of those parameter parameters should also be "
                           "specified: -s / -g / -d / -a");
  exit(-1);
}

void setupDBEditorParams(int argc, char **argv) {
  string valStr;
  char argTempBuffer[PATH_MAX + 10];
  int o = 0;
  int tempNum = 0;
  while ((o = getopt(argc, argv, "gdsa:p:")) != EOF) {
    switch (o) {
      case 'p':
        strncpy(argTempBuffer, optarg, sizeof(argTempBuffer) - 1);
        argTempBuffer[sizeof(argTempBuffer) - 1] = 0;
        dbPath << argTempBuffer;
        break;
      case 'g':
        dbOperation = GET_LAST_BLOCK_SEQ_NBR;
        break;
      case 'd':
        dbOperation = DELETE_LAST_STATE_METADATA;
        break;
      case 's':
        dbOperation = DUMP_ALL_VALUES;
        break;
      case 'a':
        strncpy(argTempBuffer, optarg, sizeof(argTempBuffer) - 1);
        argTempBuffer[sizeof(argTempBuffer) - 1] = 0;
        valStr = argTempBuffer;
        tempNum = stoi(valStr);
        if (tempNum >= 0 && tempNum < MAX_OBJECT_ID)
          numOfObjectsToAdd = tempNum;
        dbOperation = ADD_STATE_METADATA_OBJECTS;
        break;
      default:
        printUsageAndExit(argv);
    }
  }
}

void setupMetadataStorage() {
  MetadataStorage::ObjectDesc objectsDesc[MAX_OBJECT_ID];
  MetadataStorage::ObjectDesc objectDesc = {0, MAX_OBJECT_SIZE};
  for (uint32_t i = firstObjId; i < MAX_OBJECT_ID; i++) {
    objectDesc.id = i;
    objectsDesc[i] = objectDesc;
    objectIdsVector.push_back(i);
  }
  metadataStorage = new RocksDBMetadataStorage(dbClient);
  metadataStorage->initMaxSizeOfObjects(objectsDesc, MAX_OBJECT_ID);
}

void dumpObjectBuf(uint32_t objectId, char *objectData, size_t objectLen) {
  LOG_INFO(logger, "*** State metadata for object id " << objectId << " :");
  printf("0x");
  for (auto i = 0; i < objectLen; ++i) printf("%.2x", objectData[i]);
  printf("\n");
}

bool addStateMetadataObjects() {
  char objectData[MAX_OBJECT_SIZE];
  memset(objectData, 0, MAX_OBJECT_SIZE);
  LOG_INFO(logger, "*** Going to add " << numOfObjectsToAdd << " objects");
  for (uint32_t objectId = firstObjId; objectId < numOfObjectsToAdd;
       objectId++) {
    memcpy(objectData, &objectId, sizeof(objectId));
    metadataStorage->atomicWrite(objectId, objectData, MAX_OBJECT_SIZE);
    dumpObjectBuf(objectId, objectData, MAX_OBJECT_SIZE);
  }
  return true;
}

bool getLastStateMetadata() {
  uint32_t outActualObjectSize = 0;
  char outBufferForObject[MAX_OBJECT_SIZE];
  bool found = false;
  for (uint32_t i = firstObjId; i < MAX_OBJECT_ID; i++) {
    metadataStorage->read(i, MAX_OBJECT_SIZE, outBufferForObject,
                          outActualObjectSize);
    if (outActualObjectSize) {
      found = true;
      dumpObjectBuf(i, outBufferForObject, MAX_OBJECT_SIZE);
    }
  }
  if (!found) LOG_ERROR(logger, "No State Metadata objects found");
  return found;
}

bool deleteLastStateMetadata() {
  Status status = metadataStorage->multiDel(objectIdsVector);
  if (!status.isOK()) {
    LOG_ERROR(logger, "Failed to delete metadata keys");
    return false;
  }
  return true;
}

void verifyInputParams(char **argv) {
  if (dbPath.str().empty() || (dbOperation == NO_OPERATION))
    printUsageAndExit(argv);

  ifstream ifile(dbPath.str());
  if (ifile.good()) {
    return;
  }

  LOG_ERROR(logger,
            "Specified DB directory " << dbPath.str() << " does not exist.");
  exit(-1);
}

void dumpAllValues(const RocksDBClient &rocksDBClient) {
  rocksdb::Iterator *it = rocksDBClient.getNewRocksDbIterator();
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    printf("Key = 0x");
    for (int i = 0; i < it->key().size(); ++i) printf("%.2x", it->key()[i]);
    printf("\nValue = 0x");
    for (int i = 0; i < it->value().size(); ++i) printf("%.2x", it->value()[i]);
    printf("\n");
  }
  assert(it->status().ok());
  delete it;
}

int main(int argc, char **argv) {
#ifdef USE_LOG4CPP
  using namespace log4cplus;
  initialize();
  BasicConfigurator logConfig(Logger::getDefaultHierarchy(), false);
  logConfig.configure();
#endif
  setupDBEditorParams(argc, argv);
  verifyInputParams(argv);

  dbClient = new RocksDBClient(dbPath.str(), new RocksKeyComparator());
  dbClient->init();

  if (dbOperation != DUMP_ALL_VALUES) setupMetadataStorage();
  bool res = false;
  switch (dbOperation) {
    case GET_LAST_BLOCK_SEQ_NBR:
      res = getLastStateMetadata();
      break;
    case DELETE_LAST_STATE_METADATA:
      res = deleteLastStateMetadata();
      break;
    case ADD_STATE_METADATA_OBJECTS:
      res = addStateMetadataObjects();
      break;
    case DUMP_ALL_VALUES:
      dumpAllValues(*dbClient);
      break;
    default:;
  }

  string result = res ? "success" : "fail";
  LOG_INFO(logger, "*** Operation completed with result: " << result);
  return res;
}
