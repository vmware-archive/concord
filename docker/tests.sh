
#!/bin/bash
# Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

echo "Pulling samples repo.."
git clone https://github.com/vmware-samples/vmware-blockchain-samples.git
cd vmware-blockchain-samples/supply-chain
git checkout tags/v0.1-concord

echo "Building supply chain sample"
docker build -t supply-chain:latest .

echo "Startup Concord"
cd ../../
docker-compose -f docker/compose/simple4.yml up -d

attempt=0
while [ $attempt -le 59 ]; do
    attempt=$(( $attempt + 1 ))

    echo "Waiting for Concord and EthRPC to be up (attempt: $attempt)..."

    concord_logs=$(docker-compose -f docker/compose/simple4.yml logs --tail=40 concord1)
    if grep -q 'concord.ConnectionManager %% new connection added, live connections: 4' <<< $concord_logs ; then
      echo "Concord is up!"
    fi

    eth_logs=$(docker-compose -f docker/compose/simple4.yml logs --tail=5 ethrpc1)
    if grep -q 'Started Application in'  <<< $eth_logs ; then
      echo "EthRPC is up!"
      break
    fi
    sleep 2
done

echo "Running supply chain funtional test"
docker run --network compose_default -t supply-chain:latest npm run truffle:test:concordDocker
