
FROM vmwconcord/concord-builder:0.2 AS concord-build

# The build context should be the root of the repository.

## Copy the blockchain repo .git directory so submodules can run git commands
## While this is somewhat dirty it makes the relative path in
## concord/submodule/concord-bft/.git work. Alternatively, we could change all
## the paths and copy the entire blockchain dir to /blockchain. However, this
## works fine, is a one line change, and the docker image is destroyed after
## build anyway.
COPY .git /.git

WORKDIR /concord
COPY . /concord

RUN git submodule update --init --recursive
WORKDIR /concord/build

# BUG: need to reconfigure twice?
RUN cmake --no-warn-unused-cli -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -DCMAKE_BUILD_TYPE:STRING=Release -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/clang-7 -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++-7 -H/concord -B/concord/build -G "Unix Makefiles" || cmake --no-warn-unused-cli -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -DCMAKE_BUILD_TYPE:STRING=Release -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/clang-7 -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++-7 -H/concord -B/concord/build -G "Unix Makefiles"
RUN cmake -DCMAKE_C_COMPILER=/usr/bin/clang-7 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-7 -DCMAKE_BUILD_TYPE=RELWITHDEBINFO -DCMAKE_CXX_FLAGS="-march=x86-64 -mtune=generic" .. && CTEST_OUTPUT_ON_FAILURE=true make -j$(nproc) all test

## Base Run image
FROM ubuntu:18.04
LABEL Description="Concord"

ARG memleak_util=""
ARG memleak_util_cmd=""

# Get this variable into the docker image for the CMD.
ENV memleak_util_cmd="${memleak_util_cmd}"

RUN apt-get update \
    && apt-get -y install ${memleak_util} \
    bind9-host \
    dnsutils \
    libboost-atomic1.65.1 \
    libboost-chrono1.65.1 \
    libboost-date-time1.65.1 \
    libboost-filesystem1.65.1 \
    libboost-program-options1.65.1 \
    libboost-system1.65.1 \
    libboost-thread1.65.1 \
    libbz2-1.0 \
    libgmp10 \
    liblz4-1 \
    libprotobuf10 \
    libsnappy1v5 \
    libzstd1 \
    libyaml-cpp0.5v5 \
    && rm -rf /var/lib/apt/lists/*


COPY --from=concord-build /opt/protobuf/lib/* /opt/protobuf/lib/
COPY --from=concord-build /opt/grpc/lib/* /opt/grpc/lib/
COPY --from=concord-build /usr/local/lib/libprometheus* /usr/local/lib/
COPY --from=concord-build /usr/local/lib/libcryptopp* /usr/local/lib/
COPY --from=concord-build /usr/local/lib/liblog4cplus* /usr/local/lib/
COPY --from=concord-build /opt/protobuf /opt/protobuf

COPY --from=concord-build /usr/local/lib/librelic* /usr/local/lib/
COPY --from=concord-build /usr/local/lib/librocksdb.* /usr/local/lib/
COPY --from=concord-build /usr/local/lib/libsecp256k1* /usr/local/lib/
COPY --from=concord-build /usr/local/lib/libevmone* /usr/local/lib/

COPY --from=concord-build /usr/local/lib/libopentracing.so.1.5.0 /usr/local/lib
RUN ln -s /usr/local/lib/libopentracing.so.1.5.0 /usr/local/lib/libopentracing.so.1
RUN ln -s /usr/local/lib/libopentracing.so.1.5.0 /usr/local/lib/libopentracing.so

COPY --from=concord-build /usr/local/lib/libthrift-0.11.0.so /usr/local/lib
RUN ln -s /usr/local/lib/libthrift-0.11.0.so /usr/local/lib/libthrift.so

WORKDIR /concord/resources
COPY --from=concord-build /concord/build/src/concord /concord/concord
COPY --from=concord-build /concord/build/tools/conc_* /concord/
COPY --from=concord-build /concord/build/tools/ec* /concord/
COPY ./test/concord.supp /concord/concord.supp

# Copy default Concord logger configuration so logging will be sanely configured
# if no custom configuration was provided when the cluster was set up.
COPY ./resources/log4cplus.properties /concord/resources/log4cplus.properties

# Management port
EXPOSE 5458

# Ledger API service
EXPOSE 50051

# SBFT ports.
EXPOSE 3501/tcp
EXPOSE 3502/tcp
EXPOSE 3503/tcp
EXPOSE 3504/tcp
EXPOSE 3505/tcp

EXPOSE 3501/udp
EXPOSE 3502/udp
EXPOSE 3503/udp
EXPOSE 3504/udp
EXPOSE 3505/udp

# ENV
ENV LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib:/opt/grpc/lib:/opt/protobuf/lib
ENV PATH=$PATH:/usr/local/go/bin

# Mountable volumes.
VOLUME [ "/concord/config", "/concord/rocksdbdata", "/concord/log", "/concord/tls_certs" ]

# Set working directory and start.
WORKDIR /concord
CMD cp /concord/config-local/concord_with_hostnames.config \
    /concord/config-local/concord.config && \
    mkdir -p /concord/resources && \
    cp /concord/config-public/metrics_config.yaml /concord/resources && \
    /concord/config-public/find-docker-instances.sh && \
    ${memleak_util_cmd} \
    /concord/concord -c /concord/config-local/concord.config
