/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef GPRC_C_INTERNAL_STREAM_OPS_H
#define GPRC_C_INTERNAL_STREAM_OPS_H

#include <grpc-c/grpc-c.h>
#include "context.h"

/*
 * Sends initial metadata when send flag is set. Otherwise adds op to context
 * ops
 */
int gc_send_initial_metadata_internal (grpc_c_context_t *context, int send);

/*
 * Read handler. Returns data if already available or puts in a request for
 * data
 */
int 
gc_stream_read (grpc_c_context_t *context, void **output, uint32_t flags, 
		long timeout);

/*
 * Sends given data into the stream. If previous write is still pending,
 * return GRPC_C_WRITE_PENDING
 */
int 
gc_stream_write (grpc_c_context_t *context, void *input, uint32_t flags, 
		 long timeout);

/*
 * Finishes write from client
 */
int 
gc_client_stream_write_done (grpc_c_context_t *context, uint32_t flags, 
			     long timeout);

/*
 * Finishes stream from client
 */
int gc_client_stream_finish (grpc_c_context_t *context, 
			     grpc_c_status_t *status, uint32_t flags);

/*
 * Finishes stream from server
 */
int gc_server_stream_finish (grpc_c_context_t *context, 
			     grpc_c_status_t *status, uint32_t flags);

/*
 * Calculate timeout spec from millisecs
 */
gpr_timespec gc_deadline_from_timeout (long timeout); 

#endif
