/*
 * Copyright (c) 2000-2006, 2011, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "config.h"
#include <common/strextra.h>

/*
 * strdupf(): sprintf + strdup: two great tastes in one!
 */
char *
strdupf (const char *fmt, ...)
{
    va_list vap;
    char buf[BUFSIZ];

    va_start(vap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vap);
    va_end(vap);

    return strdup(buf);
}

