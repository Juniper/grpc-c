#
# Copyright (c) 2016, Juniper Networks, Inc.
# All rights reserved.
#

bin_PROGRAMS = protoc-gen-grpc-c

AUTOMAKE_OPTIONS = subdir-objects

PROTOC_C_PATH = ${srcdir}/../third_party/protobuf-c/protoc-c

protoc_gen_grpc_c_SOURCES = \
    ../third_party/protobuf-c/protoc-c/c_bytes_field.cc \
    ../third_party/protobuf-c/protoc-c/c_enum.cc \
    ../third_party/protobuf-c/protoc-c/c_enum_field.cc \
    ../third_party/protobuf-c/protoc-c/c_extension.cc \
    ../third_party/protobuf-c/protoc-c/c_field.cc \
    ../third_party/protobuf-c/protoc-c/c_file.cc \
    ../third_party/protobuf-c/protoc-c/c_generator.cc \
    ../third_party/protobuf-c/protoc-c/c_helpers.cc \
    ../third_party/protobuf-c/protoc-c/c_message.cc \
    ../third_party/protobuf-c/protoc-c/c_message_field.cc \
    ../third_party/protobuf-c/protoc-c/c_primitive_field.cc \
    ../third_party/protobuf-c/protoc-c/c_service.cc \
    ../third_party/protobuf-c/protoc-c/c_string_field.cc \
    grpc_c_file.cc \
    grpc_c_generator.cc \
    grpc_c_helpers.cc \
    grpc_c_message.cc \
    grpc_c_service.cc \
    main.cc

protoc_gen_grpc_c_LDADD = \
    ${prefix}/lib/libprotobuf.a \
    ${prefix}/lib/libprotoc.a

AM_CXXFLAGS = \
    -I${srcdir} \
    -I${srcdir}/../third_party/protobuf-c \
    -I${srcdir}/../third_party/protobuf-c/protobuf-c

