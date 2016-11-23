/*
 * Copyright (c) 2000-2006, 2012, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include <stdarg.h>

#include "config.h"
#include <common/strextra.h>

/*
 * snprintf_safe
 *
 * This convenience function clips the return value of snprintf to
 * 0 <= retval <= 'outsize'.  This is useful if snprintf needs to be called
 * in sequence without doing handling error conditions at each step.
 * Example:
 *
 * char buf[MAX];
 * char *ep = buf + sizeof(buf);
 * char *out = buf;
 * out += snprintf_safe(out, ep - out, "...);
 * out += snprintf_safe(out, ep - out, "...);
 * ...
 * if ((ep - out) == 0)
 *    somewhere along the way we ran out of space
 */
size_t
snprintf_safe (char *out, size_t outsize, const char *fmt, ...)
{
    int status;
    size_t retval = 0;
    va_list ap;
    if (out && outsize) {
	va_start(ap, fmt);
	status = vsnprintf(out, outsize, fmt, ap);
	if (status < 0) { /* this should never happen, */
	    *out = 0;     /* handle it in the safest way possible if it does */
	    retval = 0;
	} else {
	    retval = status;
	    retval = retval > outsize ? outsize : retval;
	}
	va_end(ap);
    }
    return retval;
}
