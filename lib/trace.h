/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved
 */

#ifndef GRPC_C_INTERNAL_TRACE_H
#define GRPC_C_INTERNAL_TRACE_H

/*
 * Flag that enables grpc-c traces
 */
extern int gc_trace;

/*
 * Initialize tracing
 */
void grpc_c_trace_init (void);

#endif /* GRPC_C_INTERNAL_TRACE_H */
