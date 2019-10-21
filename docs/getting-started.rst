.. getting-started

.. highlight:: shell
Getting Started
===============

The preferred way to deploy and develop on Concord is through the use of `Docker <https://www.docker.com/>`_,
which should be all you need to get a deployment of Concord running locally. 

Once you have Docker installed, checkout the Concord repository:: 

   git clone https://github.com/vmware/concord.git

After that, from the Concord directory, run the build script for the Docker images:: 

   cd concord
   docker/build.images.sh

You're now ready to launch Concord. Locally, ``docker-compose`` makes it very easy to setup a
Concord cluster. 

.. attention:: The following instructions setup Concord to use insecure (http) communication
               for simplicity and should not be used in production.

To launch a simple 4-node Concord cluster, launch the ``simple4.yml`` file:: 

   docker-compose -f docker/compose/simple4.yml up

You should see some messages go by, indicating the cluster is starting up. Once the cluster
is up, the next setup is to interact with it. Since this cluster comes with the ``ethRPC`` bridge,
we can communciate with it using standard Ethereum tooling.

.. attention:: The ``ethRPC`` bridge currently only supports Truffle 4.x. Connecting with a newer version
               of Truffle will result in connection errors.

`Truffle <https://www.npmjs.com/package/truffle>`_ is a popular tool for developing and debugging
Ethereum smart contracts. If you're familar with Truffle (note that we only support Truffle 4.x at the moment)
and have used it before, you can connect to the blockchain instance on your local machine 
at http://localhost:8545, which will connect to the first Concord node in your system.  

Otherwise, the repository contains a docker image with Truffle pre-installed to deploy to the simple
4 node Concord instance you just created. To start the image and connect to the first Concord node, run:: 
 
   docker run -it concord-truffle:latest
   truffle console  --network ethrpc1

.. highlight:: javascript
Now you can run a test transaction using Truffle. Type the following in the Truffle console:: 
 
   var accounts = await web3.eth.getAccounts()
   await web3.eth.sendTransaction({from: accounts[0], to: accounts[1], value: web3.utils.toWei("0", "ether")})   
   web3.eth.getTransaction(tx)

You should get ouptut similar to::

   {
   blockHash: '0xb3bb6deed1446b30dcdaec50faf1b7fe40f8543bccb552743d4869cab5b50e63',
   logsBloom: '0x00',
   gasUsed: 10000000,
   blockNumber: 6,
   cumulativeGasUsed: 10000000,
   contractAddress: null,
   transactionIndex: 0,
   from: '0x262c0d7ab5ffd4ede2199f6ea793f819e1abb019',
   to: '0x5bb088f57365907b1840e45984cae028a82af934',
   logs: [],
   transactionHash: '0x9a6f2ae673da7454d66c74116183f680b0ea5d06e49f04bbcd312f0b362ac705',
   status: true
   }


This creates a transaction which takes 0 Ether from test account 0 and sends it to test account 1, and prints
out the information of the resulting transaction. Congratulations, you've just executed your first Ethereum
transaction on Concord!

From here, you can look into how to :ref:`deploy <deployment>` Concord, read the :ref:`tutorials <tutorials>` on how
to install and interact with smart contracts on Concord, or learn how to :ref:`develop <develop>` support for your 
own custom API on Concord.


