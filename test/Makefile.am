#
# Copyright (c) 2017, Juniper Networks, Inc.
# All rights reserved.
#

SUBDIRS = end2end interop

test:
	@(cur=`pwd` ; for dir in ${SUBDIRS} ; do \
	    cd $$dir ; \
	    ${MAKE} tests ; \
	    cd $$cur ; \
	done)
