FROM ubuntu:22.04

LABEL description="Linux Ubuntu 22.04 with dependencies to build gRPC example"
LABEL vendor="CPqD"

## ====================================
## General dependencies
## ====================================

RUN apt-get update && \
    apt-get -y install  \
    libgoogle-glog-dev \
    cmake \
    build-essential \
    build-essential && \
    apt-get autoremove -y

