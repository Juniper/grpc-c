# GRPC-C

C implementation of [gRPC](http://www.grpc.io/) layered of top of core libgrpc. 

##Build

```sh
autoreconf --install
git submodule update --init
mkdir build && cd build
../configure
make
sudo make install
```

##Examples

```sh
cd build/examples
make foo.grpc-c.c
make
```

This should build foo_client and foo_server. To run example code, 

```sh
sudo ./foo_server test
sudo ./foo_client test
```

##Status

Pre-alpha. Under active development. APIs may change.

##Dependencies

- gRPC master
- protobuf 3.0
- protobuf-c 1.2.1
