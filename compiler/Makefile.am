#
# Copyright (c) 2016, Juniper Networks, Inc.
# All rights reserved.
#

bin_PROGRAMS = protoc-gen-grpc-c

AUTOMAKE_OPTIONS = subdir-objects

PROTOC_C_PATH = ${srcdir}/../../protobuf-c/protoc-c

protoc_gen_grpc_c_SOURCES = \
    ../../protobuf-c/protoc-c/c_bytes_field.cc \
    ../../protobuf-c/protoc-c/c_enum.cc \
    ../../protobuf-c/protoc-c/c_enum_field.cc \
    ../../protobuf-c/protoc-c/c_extension.cc \
    ../../protobuf-c/protoc-c/c_field.cc \
    ../../protobuf-c/protoc-c/c_file.cc \
    ../../protobuf-c/protoc-c/c_generator.cc \
    ../../protobuf-c/protoc-c/c_helpers.cc \
    ../../protobuf-c/protoc-c/c_message.cc \
    ../../protobuf-c/protoc-c/c_message_field.cc \
    ../../protobuf-c/protoc-c/c_primitive_field.cc \
    ../../protobuf-c/protoc-c/c_service.cc \
    ../../protobuf-c/protoc-c/c_string_field.cc \
    grpc_c_file.cc \
    grpc_c_generator.cc \
    grpc_c_helpers.cc \
    grpc_c_message.cc \
    grpc_c_service.cc \
    main.cc

protoc_gen_grpc_c_LDADD = \
    /usr/local/lib/libprotobuf.a \
    /usr/local/lib/libprotoc.a

AM_CXXFLAGS = \
    -I${srcdir} \
    -I${srcdir}/../../protobuf-c \
    -I${srcdir}/../../protobuf-c/protobuf-c

