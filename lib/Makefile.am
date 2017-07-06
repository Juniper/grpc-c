#
# Copyright (c) 2016, Juniper Networks, Inc.
# All rights reserved.
#

lib_LTLIBRARIES = libgrpc-c.la

AUTOMAKE_OPTIONS = subdir-objects

libgrpc_c_la_SOURCES = \
    client.c \
    common/snprintf_safe.c \
    common/strdupf.c \
    common/strndup.c \
    context.c \
    hooks.c \
    memory.c \
    metadata_array.c \
    service.c \
    stream_ops.c \
    thread_pool.c \
    trace.c

AM_CFLAGS = \
    -I${srcdir} \
    -I${srcdir}/h \
    -I${srcdir}/../third_party/protobuf-c

install-exec-hook:
	@echo Installing header files ... ; \
	    ${MKDIR} -p ${includedir}/grpc-c ; \
	    ${CP} ${srcdir}/h/grpc-c/grpc-c.h ${includedir}/grpc-c/grpc-c.h ;
