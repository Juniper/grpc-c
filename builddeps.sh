#
# Copyright (c) 2018, Juniper Networks, Inc.
# All rights reserved.
#

#!/bin/sh
pfix=/usr/local
if [ $# -gt 0 ]
then
    pfix=$1
fi

echo "Building gRPC"
cd third_party/grpc
git submodule update --init
make && sudo make prefix=${pfix} install
cd ../../

echo "Installing Protobuf"
cd third_party/protobuf
./autogen.sh && ./configure --prefix=${pfx} && make && sudo make prefix=${pfix} install
sudo ldconfig
cd ../../

echo "Building protobuf-c"
cd third_party/protobuf-c
./autogen.sh && ./configure --prefix=${pfix} && make && sudo make install
cd ../../

