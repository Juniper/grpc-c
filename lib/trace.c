/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved
 */

#include <stdio.h>
#include <syslog.h>

#include <grpc/support/log.h>
#include <grpc-c/grpc-c.h>

#include "trace.h"

/*
 * Trace function callback
 */
static grpc_c_trace_callback_t *trace_cb = NULL;

/*
 * Internal grpc-c trace callback that gets registered into grpc context
 */
static void
gc_gpr_log (gpr_log_func_args *args)
{
    int priority = 0;
    const char *fname;
    char *rslash;

    rslash = strrchr(args->file, '/');
    if (rslash == NULL) {
	fname = args->file;
    } else {
	fname = rslash + 1;
    }

    /*
     * If user sets tracing callback, call it with severity, filename, line
     * number and message. Otherwise write message to stderr
     */
    if (trace_cb) {
	switch (args->severity) {
	    case GPR_LOG_SEVERITY_DEBUG:
		priority = LOG_DEBUG;
		break;
	    case GPR_LOG_SEVERITY_INFO:
		priority = LOG_INFO;
		break;
	    case GPR_LOG_SEVERITY_ERROR:
		priority = LOG_ERR;
		break;
	}
	trace_cb(priority, fname, args->line, args->message);
    } else {
	fprintf(stderr, "%s %s:%d %s\n", 
		gpr_log_severity_string(args->severity), 
		fname, args->line, args->message);
    }
}

/*
 * Enable/disable tracing by given flags
 */
static void
gc_trace_enable_by_flag (int flags, int enabled)
{
    if (flags & GRPC_C_TRACE_ALL) {
	grpc_tracer_set_enabled("all", enabled);

	/*
	 * grpc_tracer_set_enabled for all enables all even if we mark for
	 * disabling. We disable everything explicitly in this case
	 */
	if (enabled) return;
    }

    if (flags & GRPC_C_TRACE_TCP) {
	grpc_tracer_set_enabled("tcp", enabled);
    }

    if (flags & GRPC_C_TRACE_CHANNEL) {
	grpc_tracer_set_enabled("channel", enabled);
    }
    
    if (flags & GRPC_C_TRACE_SURFACE) {
	grpc_tracer_set_enabled("surface", enabled);
    }
    
    if (flags & GRPC_C_TRACE_HTTP) {
	grpc_tracer_set_enabled("http", enabled);
    }
    
    if (flags & GRPC_C_TRACE_FLOWCTL) {
	grpc_tracer_set_enabled("flowctl", enabled);
    }
    
    if (flags & GRPC_C_TRACE_BATCH) {
	grpc_tracer_set_enabled("batch", enabled);
    }
    
    if (flags & GRPC_C_TRACE_CONNECTIVITY_STATE) {
	grpc_tracer_set_enabled("connectivity_state", enabled);
    }

    if (flags & GRPC_C_TRACE_SECURE_ENDPOINT) {
	grpc_tracer_set_enabled("secure_endpoint", enabled);
    }

    if (flags & GRPC_C_TRACE_TRANSPORT_SECURITY) {
	grpc_tracer_set_enabled("transport_security", enabled);
    }
}

/*
 * Enable tracing by flags
 */
void 
grpc_c_trace_enable (int flags)
{
    gc_trace_enable_by_flag(flags, 1);
}

/*
 * Disables tracing by flag
 */
void 
grpc_c_trace_disable (int flags)
{
    gc_trace_enable_by_flag(flags, 0);
}

/*
 * Sets grpc-c trace callback
 */
void 
grpc_c_set_trace_callback (grpc_c_trace_callback_t *fn)
{
    trace_cb = fn;
}

/*
 * Initializes tracing. Sets grpc-c trace function into grpc context
 */
void 
grpc_c_trace_init ()
{
    gpr_set_log_function(gc_gpr_log);
}
