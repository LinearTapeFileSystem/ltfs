FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get -q update && apt-get -q -y install --no-install-recommends \
        build-essential automake autoconf libtool pkg-config \
        icu-devtools libicu-dev libxml2-dev uuid-dev libsnmp-dev \
        libfuse-dev fuse3 libfuse3-dev \
        attr file procps util-linux tar \
    && rm -rf /var/lib/apt/lists/*

# configure.ac probes icu-config, which modern ICU no longer ships.
COPY icu-config /usr/local/bin/icu-config
RUN chmod +x /usr/local/bin/icu-config

WORKDIR /ltfs
