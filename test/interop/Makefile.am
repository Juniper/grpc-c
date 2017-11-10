#
# Copyright (c) 2017, Juniper Networks, Inc.
# All rights reserved.
#

bin_PROGRAMS = client server

.PHONY: gencode

# Server driver sources
server_SOURCES = server.c empty.grpc-c.c test.grpc-c.c messages.grpc-c.c test.grpc-c.service.c

# Client driver sources
client_SOURCES = client.c empty.grpc-c.c test.grpc-c.c messages.grpc-c.c

LDADD = \
    ../../lib/libgrpc-c.la \
    -lgrpc \
    -lgpr \
    -lprotobuf-c \
    -lpthread

AM_CFLAGS = \
    -I. \
    -I${curdir} \
    -I${srcdir}/../../lib/h/ \
    -I${srcdir}/../../lib/ \
    -I${srcdir}/../../third_party/protobuf-c \
    -I${srcdir}/../../third_party/grpc/include

gencode: 
	protoc -I ${srcdir}/proto --grpc-c_out=. \
	    --plugin=protoc-gen-grpc-c=../../compiler/protoc-gen-grpc-c \
	    ${srcdir}/proto/test.proto ${srcdir}/proto/empty.proto \
	    ${srcdir}/proto/messages.proto

all: gencode client server
