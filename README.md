![Concord](logoConcord.png)

# Concord

Project Concord is an open source, scalable decentralized Blockchain. It 
leverages the [Concord-BFT](https://github.com/vmware/concord-bft) engine
based on years of academic and industrial research, and implements 
a Blockchain which supports running Ethereum smart contracts.

To get started:
```bash
# Build the docker image (will take some time)
docker/build_images.sh

# Run a 4-node Concord system and the EtherRPC bridge with docker compose
docker-compose -f docker/compose/simple4.yml

# Connect to Concord with Truffle (in a different window)
docker run -it concord-truffle:latest
truffle console --network ethrpc1

# Or use your own local copy of Truffle (4.x) and connect to localhost:8545
```

Check out our [documentation](https://concord.readthedocs.io/en/latest/) for 
a quick [getting started](https://concord.readthedocs.io/en/latest/getting-started.html), detailed [installation and deployment](https://concord.readthedocs.io/en/latest/deployment/deployment.html) instructions,
[tutorials](https://concord.readthedocs.io/en/latest/tutorials/tutorials.html) for installing Ethereum smart contracts as well as how to contribute 
and [develop](https://concord.readthedocs.io/en/latest/developer/developer.html) on Concord.

If you run into issues while using Concord, take a look at the [help](https://concord.readthedocs.io/en/latest/help.html) 
section of our documentation. If you believe you have found a bug or have a feature
request, please open a [GitHub issue](https://github.com/vmware/concord/issues/new/choose).