#
# Copyright (c) 2016, Juniper Networks, Inc.
# All rights reserved.
#

AC_INIT([grpc-c], [0.1], [nitk.ajay@gmail.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign -Wno-portability])

AC_PROG_CC
AC_PROG_CXX
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_FILES([
    compiler/Makefile
    examples/Makefile
    lib/Makefile
    Makefile
])

LT_INIT([shared])

AC_CHECK_FUNCS([strnstr])
AC_CHECK_FUNCS([strndup])
AC_CHECK_FUNCS([strlcpy])

AC_OUTPUT
