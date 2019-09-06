# simpleKVBTests

These tests are used to verify correct replicas behavior with variable number
 of faulty and slow replica parameters and using persistent key-value 
 blockchain.

## Building

The tests are built together with the project. No special 
installation/configuration steps required. Files essential for tests running 
are copied to <PROJECT_DIR>/concord/build/test/simpleKVBTests directory.

## Running

The tests should be launched from 
<PROJECT_DIR>/concord/build/test/simpleKVBTests/scripts directory.
There are a few pre-defined test launching scripts that verify different 
replicas configuration. IMPORTANT: when you move from running one script to 
another, verify that the number of replicas is not changed. In case it is, 
persistent KVBs should be cleaned first, otherwise additional replicas start 
from the clean DB and their state is not the same as for the old ones.



