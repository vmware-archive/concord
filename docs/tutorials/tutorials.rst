.. _tutorials

Tutorials
=========

.. note:: The tutorials documentation is currently a work in progress. Help contribute documentation
          by submitting a pull request! You can edit this page by clicking on the ``Edit on GitHub``
          link on the top right.

Installing your first smart contract
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In this first tutorial, we'll install a simple Hello World contract into Concord and interact with it.
This tutorial assumes you've followed the steps from the getting started, 
Start by running the simple four node Concord instance, ``simple4.yml`` from the getting started file:: 

   docker-compose -f docker/compose/simple4.yml up

Next, start the truffle image::

   docker run -it concord-truffle:latest

We'll now create a simple hello world contract. We'll use ``vim`` to create the contract, which
needs to be inserted in the contracts directory::

   vi contracts/HelloWorld.sol

.. highlight:: solidity
Insert the following code and save the file::
    pragma solidity ^0.4.0;
    contract HelloWorld {
        address public creator; 
        string public message; 

        // constructor
        function HelloWorld() {
            creator = msg.sender;
            message = 'Hello, world';
        }
    }

Now, we'll need to create a javascript file to instruct truffle to deploy the contract. This file needs
to be inserted in the migrations directory::
.. highlight:: shell
  vi migrations/2_deploy_contracts.js
.. highlight:: javascript
Insert the following code and save the file::
    pragma solidity ^0.4.0;
    contract HelloWorld {
        address public creator; 
        string public message; 

        // constructor
        function HelloWorld() {
            creator = msg.sender;
            message = 'Hello, world';
        }
    }
var HelloWorld = artifacts.require("./HelloWorld.sol");

module.exports = function(deployer) {
  deployer.deploy(HelloWorld);
};

Now we can deploy the contract by running::
.. highlight:: shell
  truffle migrate --network ethrpc1

If the contract is sucessfully deployed, you should see output similar to::
    Using network 'ethrpc1'.

    Running migration: 1_initial_migration.js
    Deploying Migrations...
    ... 0xa8dd74c8e388917e73898422703f0bd7baa6abdcf663ae2f945f8d0ca03dae1e
    Migrations: 0x4c131316a325ffb02ec34c4bf2993b0cf6eea9eb
    Saving artifacts...
    Running migration: 2_deploy_contracts.js
    Deploying HelloWorld...
    ... 0x62b20c6e38ae96da3ac2dc8f698f5a4c134e331d076b462820ae05ace08300ab
    HelloWorld: 0xf85e97d2420bf900ef42bb979e6f3fcdd0351da8
