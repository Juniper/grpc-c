/*
 * Copyright (c) 2000-2006, 2011, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <sys/types.h>
#include <string.h>

#include "config.h"
#include <common/strextra.h>

#include <grpc/support/alloc.h>

#ifndef HAVE_STRNDUP
/*
 * strndup(): strdup() meets strncat(); return a duplicate string
 * of upto count characters of str. Always NUL terminate.
 */
char *
strndup (const char *str, size_t count)
{
    if (str == NULL) return NULL;
    else {
	size_t slen = strlen(str);
	size_t len = (count < slen) ? count : slen;
	char *cp = (char *) gpr_malloc(len + 1);

	if (cp) {
	    if (str) memcpy(cp, str, len);
	    cp[ len ] = 0;
	}
	return cp;
    }
}
#endif /* HAVE_STRNDUP */
