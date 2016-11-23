/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef GRPC_C_INTERNAL_HOOKS_H
#define GRPC_C_INTERNAL_HOOKS_H

#include "thread_pool.h"

/*
 * Returns client id set in channel
 */
const char *grpc_c_get_client_id_from_channel (grpc_channel *channel);

/*
 * Returns underlying grpc type
 */
gc_grpc_type_t grpc_c_get_type (void);

/*
 * Sets disconnect callback into transport
 */
void grpc_c_grpc_set_disconnect_cb (grpc_channel *channel, 
				    grpc_c_client_disconnect_callback_t *cb);

/*
 * Sets client id into transport
 */
void grpc_c_grpc_set_client_id (grpc_channel *channel, const char *id);

/*
 * Sets evcontext into libgrpc layer
 */
void grpc_c_grpc_set_evcontext (void *evcontext);

/*
 * Sets client task pointer into libgrpc layer
 */
void grpc_c_grpc_set_client_task (void *task);

/*
 * Sets callback on completion queue
 */
void grpc_c_grpc_set_cq_callback (grpc_completion_queue *cq, 
				  int (*cb)(grpc_completion_queue *cq));

/*
 * Returns created threadpool
 */
grpc_c_thread_pool_t *grpc_c_get_thread_pool (void);

#endif /* GRPC_C_INTERNAL_HOOKS_H */
