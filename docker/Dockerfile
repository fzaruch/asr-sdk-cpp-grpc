FROM ubuntu:22.04

LABEL description="Linux Ubuntu 22.04 with dependencies to build gRPC example"
LABEL vendor="CPqD"

ARG JOBS=4

ENV JOBS=${JOBS}

## ====================================
## General dependencies
## ====================================

RUN apt-get update && \
    apt-get -y install  \
    libgoogle-glog-dev \
    cmake \
    build-essential \
    autoconf \
    git \
    libtool \
    pkg-config \
    libboost-dev && \
    apt-get autoremove -y

RUN git clone --recurse-submodules -b v1.58.0 --depth 1 --shallow-submodules https://github.com/grpc/grpc

# removed cmake configs
RUN bash -c "cd grpc && \
    mkdir -p cmake/build && \
    pushd cmake/build && \
    cmake -DgRPC_INSTALL=ON \
          -DgRPC_BUILD_TESTS=OFF \
          ../.. && \
    make -j ${JOBS} && \
    make install && \
    popd"

RUN rm -rf /grpc/cmake/build
