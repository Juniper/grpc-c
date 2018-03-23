#
# Copyright (c) 2017, Juniper Networks, Inc.
# All rights reserved.
#

bin_PROGRAMS = tester client server

PROTOC = protoc

.PHONY: gencode tests

GRPC_C_END2END_TESTS = \
	unary_sync

# Generate code from proto files present as part of tests
PROTOS = `ls -1 ${srcdir}/*.proto`
PROTO_FILENAMES = $(basename $(notdir ${PROTOS}))
SERVER = $(CURDIR)/server
CLIENT = $(CURDIR)/client
TESTER = $(CURDIR)/tester

# Server driver sources
server_SOURCES = server.c

# Generated grpc-c code
server_SOURCES += unary_sync.grpc-c.c

# Generated grpc-c service stubs
server_SOURCES += unary_sync.grpc-c.service.c

# Server test logic
server_SOURCES += unary_sync_server.c

# Client driver sources
client_SOURCES = client.c

# Generated grpc-c code
client_SOURCES += unary_sync.grpc-c.c

# Client test logic
client_SOURCES += unary_sync_client.c

tester_SOURCES = tester.c

%.grpc-c.c %.grpc-c.service.c:
	@(for protofile in ${PROTOS} ; do \
	    ${PROTOC} -I ${srcdir} --grpc-c_out=. \
	    --plugin=protoc-gen-grpc-c=../../compiler/protoc-gen-grpc-c \
	    $$protofile; \
	done)

gencode:
	@(for protofile in ${PROTOS} ; do \
	    ${PROTOC} -I ${srcdir} --grpc-c_out=. \
	    --plugin=protoc-gen-grpc-c=../../compiler/protoc-gen-grpc-c \
	    $$protofile; \
	done)

gc-end2end-tests.h:
	@rm -f gc-end2end-tests.h
	@echo '#define GRPC_C_END2END_TEST_${PROG}' > gc-end2end-tests.h

tests: $(foreach name,${GRPC_C_END2END_TESTS},${name}_test)

LDADD = \
    ../../lib/libgrpc-c.la \
    -lgrpc \
    -lgpr \
    -lprotobuf-c \
    -lpthread

AM_CFLAGS = \
    -I${srcdir}/../../lib/h/ \
    -I${srcdir}/../../lib/ \
    -I${srcdir}/../../third_party/protobuf-c \
    -I${srcdir}/../../third_party/grpc/include

#
# Indiviual test targets
#
$(foreach name,${GRPC_C_END2END_TESTS},${name}_test):
	@echo Running $@
	LD_LIBRARY_PATH=${prefix}/lib ${TESTER} ${@:_test=} 127.0.0.1:50000

all: gencode tester client server tests
