
#!/bin/bash
# Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

echo "Pulling samples repo.."
git clone https://github.com/vmware-samples/vmware-blockchain-samples.git
cd vmware-blockchain-samples/supply-chain
git checkout mharrison/functional-tests-on-concord

echo "Building Supply Chain Sample"
docker build -t supply-chain:latest .

echo "Startup concord"
cd ../../
docker-compose -f docker/compose/simple4.yml up -d
sleep 30

echo "Running supply chain funtional test"
docker run --network compose_default -t supply-chain:latest truffle test --network=concordDocker
