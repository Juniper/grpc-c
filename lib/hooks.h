/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef GRPC_C_INTERNAL_HOOKS_H
#define GRPC_C_INTERNAL_HOOKS_H

#include "thread_pool.h"

/*
 * Returns underlying grpc type
 */
gc_grpc_type_t grpc_c_get_type (void);

/*
 * Sets disconnect callback into transport
 */
void 
grpc_c_grpc_set_disconnect_cb (grpc_call *call, 
			       grpc_c_client_disconnect_callback_t *cb);

/*
 * Gets client id from call
 */
const char *
grpc_c_grpc_get_client_id (grpc_call *call);

/*
 * Sets client id into transport
 */
void grpc_c_grpc_set_client_id (grpc_call *call, const char *id);

/*
 * Sets function that gets called when socket fd is created
 */
void 
grpc_c_grpc_set_client_socket_create_callback (void (*fp)(int fd, 
							  const char *uri));

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
 * Attempts to connect to server from client
 */
int 
grpc_c_grpc_client_try_connect (long timeout, void (*timeout_cb)(void *data), 
				void *timeout_cb_arg, void **tag);

/*
 * Cancels a connection retry attempt to server from client
 */
void grpc_c_grpc_client_cancel_try_connect (void *closure);

/*
 * Returns created threadpool
 */
grpc_c_thread_pool_t *grpc_c_get_thread_pool (void);

#endif /* GRPC_C_INTERNAL_HOOKS_H */
