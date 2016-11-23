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
    service.c \
    thread_pool.c \
    trace.c

AM_CFLAGS = \
    -I${srcdir} \
    -I${srcdir}/h \
    -I${srcdir}/../third_party/protobuf-c

libgrpc_c_la_LIBADD = \
    /usr/local/lib/libgrpc.a \
    /usr/local/lib/libgpr.a
