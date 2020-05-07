# This docker base image is used to build Concord.
# It contains all the necessary utilities needed to build Concord, and
# is used to generate the Concord runtime image.

# The build context should be the root of the repository.

FROM ubuntu:18.04 as concord-builder

ARG MAKE_PARALLELISM=4

RUN apt-get update && apt-get -y install \
    autoconf \
    automake \
    build-essential \
    clang-7 \
    clang-format-7 \
    g++ \
    git \
    libboost1.65-dev \
    libboost-program-options1.65-dev \
    libboost-system1.65-dev \
    libboost-filesystem1.65-dev \
    libboost-thread1.65-dev \
    libbz2-dev \
    libgmp3-dev \
    liblz4-dev \
    libprotobuf-dev \
    libssl-dev \
    libsnappy-dev \
    libtool \
    libzstd-dev \
    libyaml-cpp0.5v5 \
    libyaml-cpp-dev \
    pkg-config \
    protobuf-compiler \
    python2.7 \
    python3-pip \
    wget \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

RUN ln -s /usr/bin/clang-format-7 /usr/bin/clang-format
RUN ln -s /usr/bin/clang-format-diff-7 /usr/bin/clang-format-diff

RUN pip3 install cpplint

WORKDIR /
RUN mkdir cmake &&\
    cd cmake && \
    wget https://cmake.org/files/v3.17/cmake-3.17.0.tar.gz && \
    tar -xzvf cmake-3.17.0.tar.gz && \
    cd cmake-3.17.0  && \
    ./bootstrap && \
    make -j${MAKE_PARALLELISM} && \
    make install && \
    cd / && \
    rm -rf cmake

# curl (with SSL) is needed for evmone
WORKDIR /
RUN mkdir curl && \
    cd curl && \
    wget https://curl.haxx.se/download/curl-7.66.0.tar.gz && \
    tar xvzf curl-7.66.0.tar.gz && \
    cd curl-7.66.0 && \
    ./configure --with-ssl && \
    make && \
    make install && \
    cd / && \
    rm -rf curl

WORKDIR /
RUN git clone https://github.com/google/googletest.git && \
    cd /googletest && \
    git checkout e93da23920e5b6887d6a6a291c3a59f83f5b579e
WORKDIR /googletest/_build
RUN cmake -DCMAKE_CXX_FLAGS="-std=c++11 -march=x86-64 -mtune=generic" .. && make -j${MAKE_PARALLELISM}

WORKDIR /
RUN git clone https://github.com/weidai11/cryptopp.git && \
    cd /cryptopp && \
    git checkout CRYPTOPP_8_2_0 && \
    CXX_FLAGS="-march=x86-64 -mtune=generic" make -j4 && \
    make install && \
    cd / && \
    rm -r cryptopp
    
WORKDIR /
RUN git clone https://github.com/log4cplus/log4cplus.git --recursive && \
    cd /log4cplus && \
    git checkout REL_2_0_4 && \
    autoconf && \
    ./configure CXXFLAGS="--std=c++11 -march=x86-64 -mtune=generic" \
                --enable-static
# Combining the RUN above with the RUN below causes the log4cplus
# build to complain about missing aclocal-1.14.
WORKDIR /log4cplus
RUN make -j4 && \
    make install && \
    cd / && \
    rm -r log4cplus
    
WORKDIR /
RUN git clone https://github.com/relic-toolkit/relic && \
    cd /relic && \
    git checkout 0998bfcb6b00aec85cf8d755d2a70d19ea3051fd
WORKDIR /relic/build
RUN cmake -DALLOC=AUTO -DWSIZE=64 -DWORD=64 -DRAND=UDEV -DSHLIB=ON -DSTLIB=ON -DSTBIN=OFF -DTIMER=HREAL -DCHECK=on -DVERBS=on -DARITH=x64-asm-254 -DFP_PRIME=254 -DFP_METHD="INTEG;INTEG;INTEG;MONTY;LOWER;SLIDE" -DCOMP="-O3 -funroll-loops -fomit-frame-pointer -finline-small-functions -march=x86-64 -mtune=generic" -DFP_PMERS=off -DFP_QNRES=on -DFPX_METHD="INTEG;INTEG;LAZYR" -DPP_METHD="LAZYR;OATEP" .. && make -j${MAKE_PARALLELISM} && make install && cd / && rm -rf /relic

WORKDIR /
RUN wget https://github.com/facebook/rocksdb/archive/v5.7.3.tar.gz \
    && tar -xzf v5.7.3.tar.gz \
    && rm v5.7.3.tar.gz
WORKDIR /rocksdb-5.7.3
RUN PORTABLE=1 make -j${MAKE_PARALLELISM} shared_lib && PORTABLE=1 make install-shared && \
    cd / && rm -rf /rocksdb-5.7.3

WORKDIR /
RUN git clone https://github.com/bitcoin-core/secp256k1 && \
    cd /secp256k1 && \
    git checkout 1e6f1f5ad5e7f1e3ef79313ec02023902bf8175c
WORKDIR /secp256k1
RUN ./autogen.sh && ./configure --enable-module-recovery CFLAGS="-march=x86-64 -mtune=generic" CPPFLAGS="-march=x86-64 -mtune=generic"
RUN make -j${MAKE_PARALLELISM} && make install && cd / \
    rm -rf /secp256k1

WORKDIR /
RUN git clone https://github.com/openssl/openssl.git && \
    cd /openssl && \
    git checkout OpenSSL_1_1_1a
WORKDIR /openssl
RUN ./config && \
    make -j${MAKE_PARALLELISM} && \
    make test && \
    make install_sw && cd / && rm -rf openssl

RUN cd / && \
    git clone https://github.com/protocolbuffers/protobuf.git && \
    cd /protobuf && \
    git checkout 3.6.x && \
    ./autogen.sh && \
    ./configure --prefix=/opt/protobuf && \
    make -j${MAKE_PARALLELISM} && \
    make install && \
    cd / && rm -rf /protobuf

WORKDIR /
RUN git clone https://github.com/ethereum/evmone.git && \
    cd /evmone && \
    git checkout v0.2.0 && \
    mkdir build && \
    git submodule update --init --recursive
WORKDIR /evmone/build

RUN cmake -DBUILD_SHARED_LIBS=ON -DCMAKE_CXX_FLAGS="-march=x86-64 -mtune=generic" .. && \
cmake --build . --config RelWithDebInfo -- -j${MAKE_PARALLELISM} && \
make install


# thrift is a jaeger dependency
WORKDIR /
RUN wget http://apache.mirrors.hoobly.com/thrift/0.11.0/thrift-0.11.0.tar.gz && \
    tar xzf thrift-0.11.0.tar.gz && \
    cd thrift-0.11.0 && \
    ./configure && \
    make -j4 && \
    make install && \
    cd .. && \
    rm -r thrift-0.11.0

# opentracing is a jaeger dependency
WORKDIR /
RUN git clone https://github.com/opentracing/opentracing-cpp && \
    cd opentracing-cpp && \
    git checkout v1.5.0 && \
    mkdir build && \
    cd build && \
    cmake .. && \
    make -j4 && \
    make install && \
    cd ../.. && \
    rm -r opentracing-cpp

# json is a jaeger dependency (and also a concord dependency, and the versions must match)
WORKDIR /
RUN git clone https://github.com/nlohmann/json && \
    cd json && \
    git checkout v3.7.3 && \
    mkdir build && \
    cd build && \
    cmake .. && \
    make -j4 && \
    make install && \
    cd ../.. && \
    rm -r json

# Jaeger build notes:
#
# * Jaeger wants to use the Hunter dependency manager, but this
#   ignores the dependencies (like Boost) we have already
#   installed. So, this build the deps manually for now.
#
# * By the same token, concord needs a Findthrift.cmake, so we copy
#   Jaeger's to the common cmake location.
WORKDIR /
RUN git clone https://github.com/jaegertracing/jaeger-client-cpp && \
    cd jaeger-client-cpp && \
    git checkout v0.5.0 && \
    mkdir build && \
    cd build && \
    cmake -DHUNTER_ENABLED=NO -DBUILD_TESTING=NO -DBUILD_SHARED_LIBS=NO -DJAEGERTRACING_BUILD_EXAMPLES=NO .. && \
    make -j4 && \
    make install && \
    cp ../cmake/Findthrift.cmake /usr/local/share/cmake-3.17/Modules/ && \
    cd ../.. && \
    rm -r jaeger-client-cpp

# Jaeger really wants to find BoostConfig.cmake, not
# FindBoost.cmake. But, this wasn't introduced until boost 1.70. This
# doesn't matter anyway, because the jaegertracing.cmake finds
# FindBoost.cmake first anyway. The following sed just removes the
# search for BoostConfig.cmake.
RUN sed -i '/boost_components/d' /usr/local/lib/cmake/jaegertracing/jaegertracingConfig.cmake

ENV LD_LIBRARY_PATH /usr/local/lib

# Install Prometheus-cpp
WORKDIR /
RUN git clone https://github.com/jupp0r/prometheus-cpp.git && \
    cd prometheus-cpp && \
    git checkout v0.8.0 && \
    git submodule init && \
    git submodule update && \
    mkdir _build && \
    cd _build && \
    cmake -DBUILD_SHARED_LIBS=ON .. && \
    make && \
    make install && \
    cd ../.. && \
    rm -r prometheus-cpp

# Install GRPC, needed for thin-client
WORKDIR /
RUN git clone https://github.com/grpc/grpc && \
    cd /grpc && \
    git checkout v1.17.x && \
    git submodule update --init && \
    cd /grpc/third_party/protobuf && \
    git checkout 3.6.x && \
    cd /grpc/third_party/protobuf && \
    ./autogen.sh && \
    ./configure --prefix=/opt/protobuf && \
    make -j4 && \
    make install && \
    cd /grpc && \
    make -j4 PROTOC=/opt/protobuf/bin/protoc && \
    make prefix=/opt/grpc install && \
    cd / && \
    rm -r grpc

# install conan, required by concord-bft
RUN pip3 install conan

# needed by conan, which invoked clang without version
RUN update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-7 100 \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-7 100 \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/clang-7 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-7 100 \
    && update-alternatives --install /usr/bin/cpp cpp /usr/bin/clang++-7 100

# needed to avoid gtest thread error
RUN make -C /googletest/_build install

ENV LD_LIBRARY_PATH /usr/local/lib