#!/bin/bash
echo "Making sure no previous replicas are up..."
killall skvb_replica

# Testing a fast path - five replicas; one faulty, one slow.

# Clean persistent DB moving from one test to another:
# From <PROJECT_DIR>/concord/build/test/simpleKVBTests/scripts directory
# launch rm -fr simpleKVBTests_*

echo "Running replica 1..."
../skvb_replica -k setB_replica_ -i 0  >& /dev/null &
echo "Running replica 2..."
../skvb_replica -k setB_replica_ -i 1 >& /dev/null &
echo "Running replica 3..."
../skvb_replica -k setB_replica_ -i 2 >& /dev/null &
echo "Running replica 4..."
../skvb_replica -k setB_replica_ -i 3 >& /dev/null &
echo "Running replica 5..."
../skvb_replica -k setB_replica_ -i 4 >& /dev/null &

echo "Sleeping for 2 seconds"
sleep 2

echo "Running client!"
time ../skvb_client -f 1 -c 1 -p 1800 -i 6

echo "Finished!"
# Cleaning up
killall skvb_replica
