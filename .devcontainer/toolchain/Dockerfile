# [Choice] Debian / Ubuntu version (use Debian 11, Ubuntu 18.04/21.04 on local arm64/Apple Silicon): debian-11, debian-10, ubuntu-21.04, ubuntu-20.04, ubuntu-18.04
ARG VARIANT=ubuntu-22.04
FROM mcr.microsoft.com/vscode/devcontainers/cpp:0-${VARIANT}

RUN apt-get update && \
    export DEBIAN_FRONTEND=noninteractive && \
    apt-get -y install --no-install-recommends \
    autoconf \
    automake \
    bc \
    bison \
    cpio \
    device-tree-compiler \
    file \
    flex \
    libfl-dev \
    libglib2.0-dev \
    libssl-dev \
    libubootenv-tool \
    libyaml-dev \
    mercurial \
    python3-dev \
    rsync \
    swig \
    u-boot-tools \
    wget

COPY scripts/* /usr/local/bin/
RUN chmod +x /usr/local/bin/*