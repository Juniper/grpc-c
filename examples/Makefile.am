#
# Copyright (c) 2016, Juniper Networks, Inc.
# All rights reserved.
#

HOSTPROG=yes
PROTOC = protoc
PROTOS = `ls -1 ${srcdir}/*.proto`

bin_PROGRAMS = \
    foo_client \
    foo_server \
    bidi_streaming_client \
    bidi_streaming_server \
    client_streaming_client \
    client_streaming_server \
    server_streaming_client \
    server_streaming_server \
    unary_sync_server \
    unary_sync_client \
    unary_async_client \
    unary_async_server

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

unary_async_client_SOURCES = \
    unary_async_client.c \
    foo.grpc-c.c \
    foo.grpc-c.h

unary_async_server_SOURCES = \
    foo_server.c \
    foo.grpc-c.c \
    foo.grpc-c.h \
    foo.grpc-c.service.c

bidi_streaming_client_SOURCES = \
    bidi_streaming_client.c \
    bidi_streaming.grpc-c.c \
    bidi_streaming.grpc-c.h

bidi_streaming_server_SOURCES = \
    bidi_streaming_server.c \
    bidi_streaming.grpc-c.c \
    bidi_streaming.grpc-c.h \
    bidi_streaming.grpc-c.service.c

server_streaming_client_SOURCES = \
    server_streaming_client.c \
    server_streaming.grpc-c.c \
    server_streaming.grpc-c.h

server_streaming_server_SOURCES = \
    server_streaming_server.c \
    server_streaming.grpc-c.c \
    server_streaming.grpc-c.h \
    server_streaming.grpc-c.service.c

client_streaming_client_SOURCES = \
    client_streaming_client.c \
    client_streaming.grpc-c.c \
    client_streaming.grpc-c.h

client_streaming_server_SOURCES = \
    client_streaming_server.c \
    client_streaming.grpc-c.c \
    client_streaming.grpc-c.h \
    client_streaming.grpc-c.service.c

foo.grpc-c.c foo.grpc-c.h foo.grpc-c.service.c:
	protoc -I ${srcdir} --grpc-c_out=. \
		--plugin=protoc-gen-grpc-c=../compiler/protoc-gen-grpc-c \
	    ${srcdir}/foo.proto

bidi_streaming.grpc-c.c bidi_streaming.grpc-c.h bidi_streaming.grpc-c.service.c:
	protoc -I ${srcdir} --grpc-c_out=. \
		--plugin=protoc-gen-grpc-c=../compiler/protoc-gen-grpc-c \
	    ${srcdir}/bidi_streaming.proto

server_streaming.grpc-c.c server_streaming.grpc-c.h server_streaming.grpc-c.service.c:
	protoc -I ${srcdir} --grpc-c_out=. \
		--plugin=protoc-gen-grpc-c=../compiler/protoc-gen-grpc-c \
	    ${srcdir}/server_streaming.proto

client_streaming.grpc-c.c client_streaming.grpc-c.h client_streaming.grpc-c.service.c:
	protoc -I ${srcdir} --grpc-c_out=. \
		--plugin=protoc-gen-grpc-c=../compiler/protoc-gen-grpc-c \
	    ${srcdir}/client_streaming.proto

gencode:
	@(for protofile in ${PROTOS} ; do \
	    ${PROTOC} -I ${srcdir} --grpc-c_out=. \
	    --plugin=protoc-gen-grpc-c=../compiler/protoc-gen-grpc-c \
	    $$protofile; \
	done)

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
