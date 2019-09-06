// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

/** @file UnitTestDriver.cpp
 *  @brief Contains unit tests for the InMemoryDBClient and RocksDBClient
 *  implementations.
 *
 *  Unit tests for the RocksDBClient.cc and InMemoryDBClient.cc
 *  Tests random reads, writes, deletes, get on empty database, write with
 *  existing key and null reads and writes.
 *
 *  Command for running :
 *  ./UnitTestDriver [-d database path (required for RocksDB)] [-m mode (1 :
 *  RocksDB, 2 : InMemoryDB, 3 : Both)] [-s seed for pseudorandom number
 *  generator]
 *
 *  Note : For these tests, the type is always E_DB_KEY_TYPE_KEY and the
 *  block id is always 0.
 *
 *  The randomly generated keys are between 0 and 100000. This means
 *  that they are always 5 digit numbers. They are converted to strings
 *  and inserted into a composite key.
 */

#include <BlockchainDBAdapter.h>
#include <unistd.h>
#include <iostream>
#include <map>
#include "../HexTools.h"
#include "Comparators.h"
#include "InMemoryDBClient.h"
#include "RocksDBClient.h"

#define PATH_MAX 4096  // Max path length

/**
 * Keys must be at least 12 bytes in length:
 *  - sizeof(EDBKeyType)=4 bytes for the key type, and
 *  - sizeof(BlockId)=8 bytes for the block ID
 * (See concord::consensus::genDbKey in BlockchainDBAdapter.cpp)
 *
 * The type goes in the first bytes, and the block id goes in the last bytes,
 * with whatever key you want to generate in the middle.
 */
#define KV_LEN 17  // 4 bytes of type + 5 bytes of key + 8 bytes of block id
#define MAX_KEYS 100000       // Max unique keys (99999 = 5 bytes)
#define MAX_VALUE 100000      // Max unique values
#define NUMBER_OF_TESTS 5000  // Total number of random operations

using namespace std;

using concord::consensus::EDBKeyType;
using concord::consensus::RocksDBClient;
using concord::consensus::RocksKeyComparator;
using concord::consensus::Status;

struct SimpleKey {
  char key[KV_LEN];

  /**@brief Custom comparator.
   * Required for local map.
   * @param rhs Other key to be compared.
   * @return True if current key is smaller than other key.
   */
  bool operator<(const SimpleKey &rhs) const {
    Slice aKey = extractKeyFromKeyComposedWithBlockId(Slice(this->key, KV_LEN));
    Slice bKey = extractKeyFromKeyComposedWithBlockId(Slice(rhs.key, KV_LEN));
    int keyComp = aKey.compare(bKey);
    return keyComp < 0;
  }
};

struct SimpleVal {
  char val[KV_LEN];
};

static map<SimpleKey, SimpleVal> m_map;  // Local map.
static int writeCount;     // Counts number of write requests handled.
static int writeNewCount;  // Counts number of new keys inserted.
static int readCount;      // Counts number of explicit read requests handled.
static int deleteCount;    // Counts number of delete requests handled.
static int writeExistingCount;  // Counts number of rewrite attempts.
static int keyCount;            // Counts Number of keys in database.
static int testCount;           // Counts number of tests executed.

/**
 * @brief Initializes static variables.
 */
void initializeStaticVariables() {
  testCount = 0;
  keyCount = 0;
  writeCount = 0;
  writeNewCount = 0;
  readCount = 0;
  deleteCount = 0;
  writeExistingCount = 0;
  m_map.clear();
}

/**
 * @brief Moves iterator to a random test key in the local map.
 *
 * @param it Iterator.
 */
void moveIteratorToRandomKey(map<SimpleKey, SimpleVal>::iterator *it) {
  // Move iterator to random test key.
  *it = m_map.begin();  // Move iterator to start of local map.
  advance(*it, (rand() % m_map.size()));  // Advance iterator randomly.
}

/**
 * @brief Fires a get request on an empty database.
 *
 * Should be called before any write operations are performed.
 *
 * @param db Database handle.
 */
void testGetOnEmptyDatabase(IDBClient *db) {
  cout << "Testing get on an empty database" << endl;

  int randomKey = rand() % MAX_KEYS;
  string key = to_string(randomKey);
  SimpleKey randomKeyObj;
  strcpy(randomKeyObj.key, key.c_str());

  Slice randomKeySlice = Slice(randomKeyObj.key, sizeof(randomKey));
  Slice *responseSlice = new Slice();

  cout << "Trying to read " << sliceToString(randomKeySlice) << endl;

  // Read from database with this key.
  Status s2 = db->get(randomKeySlice, *responseSlice);
  assert(s2.IsNotFound());

  cout << "Key not found. Test passed!" << endl;
}

/**
 * @brief Tests the get method.
 *
 * Selects a random key from the local map and reads its value from the
 * database. Compares the values to check consistency.
 *
 * @param it Iterator.
 * @param db Database handle.
 */
void testGet(map<SimpleKey, SimpleVal>::iterator &it, IDBClient *db) {
  cout << "Test read" << endl;
  readCount++;
  int size = m_map.size();

  if (size == 0) {
    assert(keyCount == 0);
    cout << "Nothing written yet" << endl;
    return;
  }

  // Move the iterator to a random key in the local map.
  moveIteratorToRandomKey(&it);

  SimpleKey randomKey = it->first;
  SimpleVal expectedVal = it->second;

  Slice randomKeySlice = Slice(randomKey.key, sizeof(randomKey));
  Slice *responseSlice = new Slice();

  cout << "Trying to read " << sliceToString(randomKeySlice) << endl;

  // Read from database with this key.
  Status s2 = db->get(randomKeySlice, *responseSlice);
  assert(s2.ok());

  // Check whether the response slice is of the correct size
  assert(responseSlice->size() == sizeof(SimpleVal));

  // Compare the values
  if (memcmp(responseSlice->data(), expectedVal.val, responseSlice->size()) !=
      0) {
    cout << "Data not correct. Expected: "
         << sliceToString(Slice(expectedVal.val, KV_LEN))
         << " Actual: " << sliceToString(*responseSlice) << endl;
  }
  assert(memcmp(responseSlice->data(), expectedVal.val,
                responseSlice->size()) == 0);

  cout << "Data read correctly : " << sliceToString(*responseSlice) << endl;
  delete (responseSlice);
}

/**
 * @brief Tests the put method.
 *
 * Selects a random key and a random value and puts it into the database. Reads
 * the new key immediately after to check consistency.
 *
 * @param db Database handle.
 */
void testPut(IDBClient *db) {
  cout << "Test write" << endl;
  // Generate a random key and value to be inserted.
  int randomWriteKey = rand() % MAX_KEYS;
  int randomWriteValue = rand() % MAX_VALUE;
  SimpleKey randomWriteKeyObj;
  SimpleVal randomWriteValObj;

  memset(randomWriteKeyObj.key, 0, KV_LEN);
  EDBKeyType _type = EDBKeyType::E_DB_KEY_TYPE_KEY;
  memcpy(randomWriteKeyObj.key, &_type, sizeof(EDBKeyType));
  string key = to_string(randomWriteKey);
  strcpy(randomWriteKeyObj.key + sizeof(EDBKeyType), key.c_str());
  // leave blockID at 0

  memset(randomWriteValObj.val, 0, KV_LEN);
  string value = to_string(randomWriteValue);
  strcpy(randomWriteValObj.val, value.c_str());

  Slice keySlice(randomWriteKeyObj.key, sizeof(SimpleKey));
  Slice valueSlice(randomWriteValObj.val, sizeof(SimpleVal));

  cout << "Writing " << sliceToString(keySlice) << " : "
       << sliceToString(valueSlice) << endl;

  // If key already exists.
  if (m_map.find(randomWriteKeyObj) != m_map.end()) {
    /*
     * Note : Test for puts to existing keys handled in the function
     * testPutExistingKey
     */
    cout << "Key already exists" << endl;
    testCount--;
  } else {
    writeCount++;

    // Write to database.
    Status s1 = db->put(keySlice, valueSlice);

    assert(s1.ok());

    // Add new key value pair to local map.
    m_map.insert(
        pair<SimpleKey, SimpleVal>(randomWriteKeyObj, randomWriteValObj));
    cout << "Data written" << endl;
    keyCount++;
    writeNewCount++;

    // Check the write
    Slice *responseSlice = new Slice();
    Status s2 = db->get(Slice(randomWriteKeyObj.key, sizeof(SimpleKey)),
                        *responseSlice);
    assert(s2.ok());

    // Check whether the response slice is of the correct size
    assert(responseSlice->size() == sizeof(SimpleVal));

    // Compare written value with randomly generated value
    assert(memcmp(responseSlice->data(), randomWriteValObj.val,
                  responseSlice->size()) == 0);

    cout << "Data written correctly" << endl;
    delete (responseSlice);
  }
}

/**
 * @brief Tests by writing to an existing key.
 *
 * Expected behavior : Value gets overwritten.  Selects a random key from the
 * local map (existing key) and fires a put request with a random value.
 *
 * @param it Iterator.
 * @param db Database handle.
 */
void testPutExistingKey(map<SimpleKey, SimpleVal>::iterator &it,
                        IDBClient *db) {
  cout << "Test write with existing key" << endl;
  writeCount++;
  writeExistingCount++;

  int size = m_map.size();
  if (size == 0) {
    assert(keyCount == 0);
    cout << "Nothing written yet" << endl;
    return;
  }

  // Move the iterator to a random test key in the local map.
  moveIteratorToRandomKey(&it);

  SimpleKey randomExistingKey = it->first;
  int randomValue = rand() % MAX_VALUE;
  string value = to_string(randomValue);
  SimpleVal randomValObj;
  strcpy(randomValObj.val, value.c_str());
  Slice keySlice(randomExistingKey.key, sizeof(SimpleKey));

  cout << "Attempting to rewrite " << sliceToString(keySlice) << " with "
       << randomValue << endl;

  // Attempt to write to database with the same key.
  Status s3 = db->put(keySlice, (Slice(randomValObj.val, sizeof(SimpleVal))));
  assert(s3.ok());

  Slice *responseSlice = new Slice();

  // Check if new value is correctly updated in the database.
  Status s4 =
      db->get(Slice(randomExistingKey.key, sizeof(SimpleKey)), *responseSlice);
  assert(s4.ok());

  // Check whether the response slice is of the correct size
  assert(responseSlice->size() == sizeof(SimpleVal));

  // Compare written value with randomly generated value.
  assert(memcmp(responseSlice->data(), randomValObj.val,
                responseSlice->size()) == 0);

  // Update local map.
  it->second = randomValObj;
  cout << "Write with existing key handled properly." << endl;

  delete (responseSlice);
}

/**
 * @brief Tests the del method.
 *
 * Selects a random key and deletes it from the database.  Reads the same
 * immediately after to ensure deletion.
 *
 * @param it Iterator.
 * @param db Database handle.
 */
void testDel(map<SimpleKey, SimpleVal>::iterator &it, IDBClient *db) {
  // Test deletes
  cout << "Test delete" << endl;
  deleteCount++;
  int size = m_map.size();

  if (size == 0) {
    assert(keyCount == 0);
    cout << "Nothing written yet" << endl;
    return;
  }

  // Move the iterator to a random test key in the local map.
  moveIteratorToRandomKey(&it);

  SimpleKey randomKey = it->first;
  Slice randomKeySlice = Slice(randomKey.key, sizeof(randomKey));

  cout << "Trying to delete " << sliceToString(randomKeySlice) << endl;

  // Delete this key from the database.
  Status s3 = db->del(randomKeySlice);
  assert(s3.ok());

  // Try reading this key again to check whether it has been deleted correctly.
  Slice *responseSlice = new Slice();
  Status s4 = db->get(randomKeySlice, *responseSlice);
  assert(!s4.ok());

  // Remove entry from local map.
  m_map.erase(it);
  keyCount--;
  cout << "Data deleted correctly" << endl;

  delete (responseSlice);
}

/**
 * @brief Tests a DBClient implementation.
 *
 * If a db_path is passed, testing is performed for RocksDB, if not, testing is
 * performed for InMemoryDB.  Runs random reads, writes, deletes, get on empty
 * database, write with existing key and null reads and writes.
 *
 * @param db_path Directory path where database is created. (Needed only for
 *                RocksDB)
 */
void testDB(char *db_path) {
  bool isRocksDB = false;
  IDBClient *db = NULL;
  string databaseName;
  RocksKeyComparator *cmp = NULL;

  if (db_path != NULL) {
    isRocksDB = true;
  }

  if (isRocksDB) {
    cout << "Creating RocksDB database" << endl;
    cmp = new RocksKeyComparator();
    db = new RocksDBClient(db_path, cmp);
    databaseName = "RocksDB";
  } else {
    cout << "Creating In Memory Database" << endl;
    db = new InMemoryDBClient((IDBClient::KeyComparator)&InMemKeyComp);
    databaseName = "InMemory";
  }

  // Test the init method.
  Status s1 = db->init();
  assert(s1.ok());
  cout << databaseName << " Database created successfully" << endl;
  cout << endl;

  testGetOnEmptyDatabase(db);

  initializeStaticVariables();

  auto it = m_map.begin();

  cout << "*******" << databaseName
       << " : Testing random reads and "
          "writes*******"
       << endl;
  cout << endl;

  while (testCount < NUMBER_OF_TESTS) {
    int test = rand() % 4;

    if (test == 0) {
      // Test reads
      testGet(it, db);
    } else if (test == 1) {
      // Test writes
      testPut(db);
    } else if (test == 2) {
      // Test deletes
      testDel(it, db);
    } else {
      // Test writing with existing key
      testPutExistingKey(it, db);
    }
    testCount++;
    cout << databaseName << " : Test no. " << testCount << " successful"
         << endl;
    cout << endl;
  }

  cout << "*************" << databaseName
       << " : Random reads and writes "
          "successful*****************"
       << endl;
  cout << endl;

  cout << "*******************" << databaseName
       << " : Testing null "
          "read*******************"
       << endl;
  Slice *nullReadKeySlice = new Slice();
  Slice *nullReadValueSlice = new Slice();
  Status nullReadStatus = db->get(*nullReadKeySlice, *nullReadValueSlice);

  assert(!nullReadStatus.ok());
  cout << "Success!" << endl;
  cout << endl;

  cout << "*******************" << databaseName
       << " : Testing null "
          "write*******************"
       << endl;
  Slice *nullWriteKeySlice = new Slice();
  Slice *nullWriteValueSlice = new Slice();
  Status nullWriteStatus = db->put(*nullWriteKeySlice, *nullWriteValueSlice);

  // TODO : Null reads return !Status.Ok() but null writes don't.
  assert(nullWriteStatus.ok());
  cout << "Success!" << endl;
  cout << endl;

  // Test the close method.
  cout << "Closing " << databaseName << " database" << endl;
  cout << endl;
  Status s10 = db->close();
  assert(s10.ok());

  cout << "**********ALL " << databaseName
       << " TESTS PASSED "
          "SUCCESSFULLY!*********"
       << endl;
  cout << "Total no. of write requests : " << writeCount << endl;
  cout << "Total no. of (explicit) read requests : " << readCount << endl;
  cout << "Total no. of delete requests : " << deleteCount << endl;
  cout << "Total no. of new keys inserted : " << writeNewCount << endl;
  cout << "Total no. of rewrite requests : " << writeExistingCount << endl;
  cout << endl;

  // Deallocate memory
  if (isRocksDB) {
    delete (cmp);
  }
  delete (db);
  delete (nullReadKeySlice);
  delete (nullReadValueSlice);
  delete (nullWriteKeySlice);
  delete (nullWriteValueSlice);
}

/**
 * @brief Main function.
 *
 * Requires a path where the RocksDB database will be stored as a command line
 * argument.  Requires a mode flag which will decide whether tests are run
 * against RocksDB, InMemoryDB or both.
 *
 * @param argc Command line argument.
 * @param argv Command line argument.
 * @return Status.
 */
int main(int argc, char **argv) {
  int opt;
  char db_path[PATH_MAX];
  time_t timeVal = time(NULL);

  // 1 : Only RocksDB; 2 : Only InMemory; 3 : Both
  int test_mode;

  while ((opt = getopt(argc, argv, "d:m:s:")) != EOF) {
    switch (opt) {
      case 'd':
        if (optarg) strncpy(db_path, optarg, PATH_MAX);
        break;
      case 'm':
        if (optarg) test_mode = stoi(optarg);
        break;
      case 's':
        if (optarg) timeVal = stoi(optarg);
        break;
      default:
        fprintf(stderr, "%s [-d db_path] [-m test_mode] [-s seed]\n", argv[0]);
        exit(-1);
    }
  }

  if (test_mode < 1 || test_mode > 3) {
    cout << "Incorrect test mode (-m) selected. Please choose 1 for "
            "RocksDB, 2 for InMemoryDB or 3 for both"
         << endl;
    exit(-1);
  }

  // Seed pseudo random number generator.
  srand(timeVal);

  cout << "Note : Random number generator was seeded with " << timeVal << endl;
  cout << endl;

  if (test_mode == 1) {
    testDB(db_path);
  } else if (test_mode == 2) {
    testDB(NULL);
  } else {
    testDB(db_path);
    testDB(NULL);
    cout << "ALL ROCKSDB AND INMEMORYDB TESTS PASSED SUCCESSFULLY!" << endl;
    cout << endl;
  }

  cout << "ALL TESTS PASSED SUCCESSFULLY!" << endl;
  cout << "Note : Random number generator was seeded with " << timeVal << endl;
  cout << endl;
  return 0;
}
