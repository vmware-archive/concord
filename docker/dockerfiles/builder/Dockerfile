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
    && rm -rf /var/lib/apt/lists/*

RUN ln -s /usr/bin/clang-format-7 /usr/bin/clang-format
RUN ln -s /usr/bin/clang-format-diff-7 /usr/bin/clang-format-diff

RUN pip3 install cpplint

WORKDIR /
RUN mkdir cmake &&\
    cd cmake && \
    wget https://cmake.org/files/v3.15/cmake-3.15.4.tar.gz && \
    tar -xzvf cmake-3.15.4.tar.gz && \
    cd cmake-3.15.4  && \
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
    git checkout CRYPTOPP_5_6_5
## This is the reason that the concord directory must be the context
WORKDIR /cryptopp
COPY ./patches/cross-platform-cryptopp.patch .
RUN git apply --whitespace=nowarn cross-platform-cryptopp.patch
WORKDIR /cryptopp/build
RUN cmake -DCMAKE_CXX_FLAGS="-march=x86-64 -mtune=generic" .. && make -j${MAKE_PARALLELISM} && make install && cd / && rm -rf /cryptopp

WORKDIR /
RUN git clone https://github.com/log4cplus/log4cplus.git && \
    cd /log4cplus && \
    git checkout REL_1_2_1
WORKDIR /log4cplus
RUN autoconf && ./configure CXXFLAGS="--std=c++11 -march=x86-64 -mtune=generic" && make -j${MAKE_PARALLELISM} && make install && cd / && rm -rf /log4cplus

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

ENV LD_LIBRARY_PATH /usr/local/lib