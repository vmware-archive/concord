.. _develop

Developer
=========

.. note:: The developer documentation is currently a work in progress. Help contribute documentation
          by submitting a pull request! You can edit this page by clicking on the ``Edit on GitHub``
          link on the top right.

Concord is developed inside of Docker containers to simplify dependency management, and the easiest way to get 
a development environment setup for Concord is by using `Visual Studio Code <https://code.visualstudio.com/>`, 
which includes Dev Container features, making it easy to deploy and debug from within a container.

Concord has a ``devcontainer.json`` file included in the repository which will correctly setup the container
and build environment. 

Launching the Dev Container
~~~~~~~~~~~~~~~~~~~~~~~~~~~
To get a dev container environment for Concord, open your Concord checkout in Visual Studio Code. Open 
the command palette (``View > Command Palette...``) and type ``Remote Containers: Reopen in container``.

The dev container environment is setup with CMake tooling. When prompted to select a CMake kit, select ``clang-7.0``.

Code Format
~~~~~~~~~~~
We enforce code formatting through ``clang-format``. The build will fail if there are code
formatting errors. To fix formatting errors automatically, run the ``format`` target,
which will attempt to automatically fix any code formatting issues in the repository.