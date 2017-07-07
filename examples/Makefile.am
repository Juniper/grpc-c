#
# Copyright (c) 2016, Juniper Networks, Inc.
# All rights reserved.
#

HOSTPROG=yes

bin_PROGRAMS = \
    foo_client \
    foo_server \
    unary_sync_server \
    unary_sync_client

foo_client_SOURCES = \
    foo_client.c \
    foo.grpc-c.c \
    foo.grpc-c.h

foo_server_SOURCES = \
    foo_server.c \
    foo.grpc-c.c \
    foo.grpc-c.h \
    foo.grpc-c.service.c

unary_sync_client_SOURCES = \
    unary_sync_client.c \
    foo.grpc-c.c \
    foo.grpc-c.h

unary_sync_server_SOURCES = \
    foo_server.c \
    foo.grpc-c.c \
    foo.grpc-c.h \
    foo.grpc-c.service.c

foo.grpc-c.c foo.grpc-c.h foo.grpc-c.service.c:
	protoc -I ${srcdir} --grpc-c_out=. \
		--plugin=protoc-gen-grpc-c=../compiler/protoc-gen-grpc-c \
	    ${srcdir}/foo.proto

LDADD = \
    ../lib/libgrpc-c.la \
    -lgrpc \
    -lgpr \
    -lprotobuf-c \
    -lpthread

AM_CFLAGS = \
    -I. \
    -I${srcdir}/../lib/h/ \
    -I${srcdir}/../third_party/protobuf-c \
    -I${srcdir}/../third_party/grpc/include
