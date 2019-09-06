#!/bin/bash
# Testing a fast path - four replicas; one faulty, no slow.

# Clean persistent DB moving from one test to another:
# From <PROJECT_DIR>/concord/build/test/simpleKVBTests/scripts directory
# launch rm -fr simpleKVBTests_*

echo "Making sure no previous replicas are up..."
killall skvb_replica

echo "Running replica 1..."
../skvb_replica -k setA_replica_ -i 0 >& /dev/null &
echo "Running replica 2..."
../skvb_replica -k setA_replica_ -i 1 >& /dev/null &
echo "Running replica 3..."
../skvb_replica -k setA_replica_ -i 2 >& /dev/null &
echo "Running replica 4..."
../skvb_replica -k setA_replica_ -i 3 >& /dev/null &

echo "Sleeping for 2 seconds"
sleep 2

echo "Running client!"
time ../skvb_client -f 1 -c 0 -p 1800 -i 4

echo "Finished!"
# Cleaning up
killall skvb_replica
