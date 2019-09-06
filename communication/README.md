# EthRPC to Concord Communication

This subproject is a common place to define the communication
primitives that are used between EthRPC and Concord. Of primary
interest is the protocol definition in (./src/main/proto). Additional
wrappers used by EthRPC are also in the src directory.

## Building

If you are following the docker image build instructions in [the
top-level README](../README.md), the communication module will be
built automatically in each container that needs it.

If you need to build the communication module directly, use Maven:

```
mvn clean install
```
