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
int gc_stream_read (grpc_c_context_t *context, void **output, long timeout);

/*
 * Sends given data into the stream. If previous write is still pending,
 * return GRPC_C_WRITE_PENDING
 */
int gc_stream_write (grpc_c_context_t *context, void *input, long timeout);

/*
 * Finishes write from client
 */
int gc_client_stream_write_done (grpc_c_context_t *context, long timeout);

/*
 * Finishes stream
 */
int gc_client_stream_finish (grpc_c_context_t *context, 
			     grpc_c_status_t *status);

#endif
