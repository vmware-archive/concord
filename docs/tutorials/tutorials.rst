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

    pragma solidity ^0.5.8;
    contract HelloWorld {
        address public creator; 
        string public message; 

        constructor() public {
            creator = msg.sender;
            message = 'Hello, world';
        }
    }

.. highlight:: shell
Now, we'll need to create a javascript file to instruct truffle to deploy the contract. This file needs
to be inserted in the migrations directory::

  vi migrations/2_deploy_contracts.js

.. highlight:: javascript
Insert the following code and save the file::

    var HelloWorld = artifacts.require("./HelloWorld.sol");

    module.exports = function(deployer) {
    deployer.deploy(HelloWorld);
    };

.. highlight:: shell
Now we can deploy the contract by running::

  truffle migrate --network ethrpc1

If the contract is sucessfully deployed, you should see output similar to::

   Compiling your contracts...
   ===========================
   > Compiling ./contracts/HelloWorld.sol
   > Compiling ./contracts/Migrations.sol
   > Artifacts written to /truffle/build/contracts
   > Compiled successfully using:
      - solc: 0.5.8+commit.23d335f2.Emscripten.clang



   Starting migrations...
   ======================
   > Network name:    'ethrpc1'
   > Network id:      5000
   > Block gas limit: 0xf4240


   1_initial_migration.js
   ======================

      Deploying 'Migrations'
      ----------------------
      > transaction hash:    0x7a62d178231bcee0167fd82330e96781bcf15e08c186a143de68bf0ce0a163c5
      > Blocks: 0            Seconds: 0
      > contract address:    0xc2b3150D03A3320b6De3F3a3dD0fDA086C384eB5
      > block number:        1
      > block timestamp:     1571682384
      > account:             0x262C0D7AB5FfD4Ede2199f6EA793F819e1abB019
      > balance:             0.000000000000012345
      > gas used:            5449
      > gas price:           0 gwei
      > value sent:          0 ETH
      > total cost:          0 ETH


      > Saving migration to chain.
      > Saving artifacts
      -------------------------------------
      > Total cost:                   0 ETH


   2_deploy_contracts.js
   =====================

      Deploying 'HelloWorld'
      ----------------------
      > transaction hash:    0x73bc4a9e91b0da818cec84259c0faee8e46d26bdffa14d72ad51447c4e2870d6
      > Blocks: 0            Seconds: 0
      > contract address:    0x7373de9d9da5185316a8D493C0B04923326754b2
      > block number:        3
      > block timestamp:     1571682385
      > account:             0x262C0D7AB5FfD4Ede2199f6EA793F819e1abB019
      > balance:             0.000000000000012345
      > gas used:            11019
      > gas price:           0 gwei
      > value sent:          0 ETH
      > total cost:          0 ETH


      > Saving migration to chain.
      > Saving artifacts
      -------------------------------------
      > Total cost:                   0 ETH


   Summary
   =======
   > Total deployments:   2
   > Final cost:          0 ETH

Next, we'll want to interact with the contract. We can do that through the truffle
console::

  truffle console --network ethrpc1

.. highlight:: javascript
The truffle console accepts javascript as input.
We can get acceess to the HelloWorld contract through the ``HelloWorld`` variable.
We want the deployed version of the contract, which we can retrieve using the asynchronous ``deployed()`` method.
In javascript, we can use await to wait for an asynchronous function (which returns a ``Promise``) to complete::

   var app = await HelloWorld.deployed();

Now you can acceess the contract through the ``app`` variable. If you type ``app.`` and press tab, tab completion
should give you the list of functions you can call::

   truffle(ethrpc1)> app.
   app.__defineGetter__      app.__defineSetter__      app.__lookupGetter__      app.__lookupSetter__      app.__proto__
   app.hasOwnProperty        app.isPrototypeOf         app.propertyIsEnumerable  app.toLocaleString        app.toString
   app.valueOf

   app.abi                   app.address               app.allEvents             app.constructor           app.contract
   app.creator               app.message               app.send                  app.sendTransaction
   app.transactionHash
   
Try calling the message function to view the message we entered in the app.
Remember, the function is asynchronous, so use the ``await`` keyword to resolve the ``Promise``::

   await app.message.call()
   > 'Hello, world'

Congratulations, you just installed your first smart contract on Concord and made a simple call to one of its functions.

