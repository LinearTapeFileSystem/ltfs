#!/bin/sh

# This is the script for building the tree under CodeQL scanning in github flow.
# Do not use on other environments

sudo apt-get -q -y update
sudo apt-get -q -y upgrade
sudo apt-get -y install build-essential automake autoconf libtool pkg-config icu-devtools libicu-dev libxml2-dev uuid-dev fuse libfuse-dev libsnmp-dev
./autogen.sh
./configure
make
