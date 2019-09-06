.. _deployment

Deployment
==========

Currently, a Concord deployment consists of two types of nodes:

- ``Concord`` nodes, which participate in the the Byzantine consensus protocol and perform state machine replication.

- ``EthRPC`` nodes, which translate Ethereum API calls into requests that ``Concord`` nodes can understand.

A deployment needs to contain :math:`3F+1` ``concord`` nodes, where :math:`F` is the number of failures 
the deployment needs to tolerate. In a typical deployment, each ``Concord`` node will have a single, 
possibly co-located ``EthRPC`` node. Ethereum applications, such as `Truffle <https://www.npmjs.com/package/truffle>`_,
interact with the deployment through the ``EthRPC`` node.

Each node is typically deployed as a Docker container, which allows for easy deployment through tools such as
``docker-compose`` and ``Kubernetes``.

In the next sections, we detail how to setup each type of node.

Concord Nodes
-------------
Concord nodes are provided by the ``concord-node`` container. Typically, you run the container with the command::

   docker run concord-node:latest --expose 3501,3502,3503,3504,3505 -p 5848 -v <log-path>:/concord/log \
   -v <rocksdb-path>:/concord/rocksdbdata -v <localconfig-path>:/concord/config-local \
   -v <publicconfig-path>:/concord/config-public:ro -v <tlscert-path>:/concord/tls_certs:ro

The paths contain the database, log, configuration and TLS certificates for the node running in the container.

- ``<log-path>`` is a path where Concord stores debug logs.

- ``<rocksdb-path>`` is a path that contains the rocksdb database replicated by the state machine.

- ``<localconfig-path>`` is a path containing a ``concord.config`` file for configuring this Concord instance.

-  ``<publicconfig-path>`` is a path containing a ``genesis.json`` file, which describes the genesis block for
   the deployment, and a ``log4cplus.properties`` file which configures the logging level for each component.

- ``<tlscert-path>`` is a path to TLS certificates used to encrypt communication. The certificates are stored in ``pem`` format.

Ports 3501-3505 are exposed because they are specified as communication ports in the configuration files
that other nodes will communicate with. Port 5848 is published to the host, so that ``EthRPC`` nodes can easily
communicate using the hosts local IP address. 

Concord requires two sets of configuration files. The local ``concord.config`` file configures per node settings
and the public and private keys used to sign requests. The public ``dockerConfigurationInput.yml`` configure
global settings, such as the size of the cluster and its configuration. Both of these files are yaml files and
their schema is documented in the next sections. The public config also includes a ``genesis.json`` file which
defines the genesis (or initial) state of the system. This includes information about the genesis block as well
as the initial accounts, balance and storage.

The TLS certificates are stored in ``pem`` format and stored in subdirectories named after the principal id
specified in the `concord.config` file. Each principal has a client/server pair, consisting of the public ``client.cert``
and ``server.cert``, as well as a ``pk.pem`` file containing the private key. More details about the TLS certificate
directory format and repository can be found in the next sections.

A convenience tool for generating configuration files and TLS certificates simplifies deployment.

Confguration File Generator
~~~~~~~~~~~~~~~~~~~~~~~~~~~
Concord nodes each have to have their own configuration files and certificates, which may be difficult to generate
by hand. To simplify this task, we provide a convenience tool, ``conc_genconfig`` which generates configuration files
as well as the TLS certificates necessary to start a Concord deployment. ``conc_genconfig`` takes a ``yaml`` file
named ``configurationInput.yml``.

configurationInput.yml
~~~~~~~~~~~~~~~~~~~~~~

.. note:: The deployment documentation is currently a work in progress. Help contribute documentation
          by submitting a pull request! You can edit this page by clicking on the ``Edit on GitHub``
          link on the top right.

concord.config
~~~~~~~~~~~~~~

.. note:: The deployment documentation is currently a work in progress. Help contribute documentation
          by submitting a pull request! You can edit this page by clicking on the ``Edit on GitHub``
          link on the top right.

genesis.json
~~~~~~~~~~~~
.. highlight:: javascript
The ``genesis.json`` file contains the genesis (initial) state of the system. It is stored in JSON format, and
a sample file is shown below::

   {
      "config": {
         "chainId": 1,
         "homesteadBlock": 0,
         "eip155Block": 0,
         "eip158Block": 0
      },
      "alloc": {
         "262c0d7ab5ffd4ede2199f6ea793f819e1abb019": {
            "balance": "12345"
         },
         "5bb088f57365907b1840e45984cae028a82af934": {
            "balance": "0xabcdef"
         },
         "0000a12b3f3d6c9b0d3f126a83ec2dd3dad15f39": {
            "balance": "0x7fffffffffffffff"
         }
      },
      "nonce": "0x000000000000000",
      "difficulty": "0x400",
      "mixhash": "0x0000000000000000000000000000000000000000000000000000000000000000",
      "parentHash": "0x0000000000000000000000000000000000000000000000000000000000000000",
      "gasLimit": "0xf4240"
   }

TLS certificates
~~~~~~~~~~~~~~~~

.. note:: The deployment documentation is currently a work in progress. Help contribute documentation
          by submitting a pull request! You can edit this page by clicking on the ``Edit on GitHub``
          link on the top right.

EthRPC Nodes
------------
Concord nodes are provided by the ``concord-ethrpc`` container. Typically, you run the container with the command::

   docker run concord-ethrpc:latest java -jar concord-ethrpc.jar --ConcordAuthorities=<host>:<port> \
   --security.require-ssl=true --server.ssl.key-store-type=PKCS12 --server.ssl.key-store=/config/keystore.p12 \
   --server.ssl.key-store-password=Ethrpc!23 --server.ssl.key-alias=ethrpc

.. attention:: To simplify deployment, you may disable SSL by setting ``-security.require-ssl=false``. However,
               this is not recommended in production environments for security reasons.

The ``--ConcordAuthorities=<host>:<port>`` specifies the ``<host>``, a hostname or ip address and API ``<port>``
of the Concord node. The ``--security.ssl.*`` parameters specify the SSL key that the HTTPS endpoint will use. 
The defaults shown above are for the self-signed certificates provided in the ``docker/resources/config-ethrpc*``
folders. You may configure the server with your own certificate.