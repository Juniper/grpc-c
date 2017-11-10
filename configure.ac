#
# Copyright (c) 2016, Juniper Networks, Inc.
# All rights reserved.
#

AC_PREREQ(2.59)

AC_INIT([grpc-c], [0.1], [nitk.ajay@gmail.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign -Wno-portability])

case $prefix in
    NONE)
	prefix=/usr/local
	;;
esac

case $exec_prefix in
    NONE)
	exec_prefix=$prefix
	;;
esac

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_FILES([
    compiler/Makefile
    examples/Makefile
    lib/Makefile
    test/Makefile
    test/end2end/Makefile
    test/interop/Makefile
    Makefile
])

LT_INIT([shared])

AC_PATH_PROG(CP, cp, /bin/cp)
AC_PATH_PROG(MKDIR, mkdir, /bin/mkdir)

AC_PROG_LN_S

AC_STDC_HEADERS

AC_C_INLINE
AC_TYPE_SIZE_T

AC_CHECK_FUNCS([strnstr])
AC_CHECK_FUNCS([strndup])
AC_CHECK_FUNCS([strlcpy])

AC_CHECK_HEADERS([ctype.h stdio.h stdlib.h])
AC_CHECK_HEADERS([string.h sys/param.h unistd.h])

AC_CHECK_HEADERS([grpc/grpc.h], [], 
		 [AC_MSG_ERROR([Could not find grpc.h anywhere, install grpc])])

# Check for required libs
PKG_CHECK_MODULES([PROTOBUF_C], [libprotobuf-c >= 1.2.1])

AC_OUTPUT
