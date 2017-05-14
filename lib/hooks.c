/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <grpc-c/grpc-c.h>
#include <grpc/support/cpu.h>

#include "hooks.h"
#include "thread_pool.h"
#include "trace.h"

static grpc_c_hook_t gc_hook;
static grpc_c_thread_pool_t *gc_tpool;

/*
 * Initializes local copy of hook callback functions. This will get called
 * from libgrpc isc and task layer
 */
void 
grpc_c_hook_init (grpc_c_hook_t *hook)
{
    if (hook) {
	gc_hook.gch_type = hook->gch_type;
	gc_hook.gch_set_cq_callback = hook->gch_set_cq_callback;
	gc_hook.gch_get_client_id = hook->gch_get_client_id;
	gc_hook.gch_set_client_id = hook->gch_set_client_id;
	gc_hook.gch_set_disconnect_cb = hook->gch_set_disconnect_cb;
	gc_hook.gch_set_evcontext = hook->gch_set_evcontext;
	gc_hook.gch_set_client_task = hook->gch_set_client_task;
	gc_hook.gch_set_client_socket_create_cb 
	    = hook->gch_set_client_socket_create_cb;
	gc_hook.gch_client_try_connect = hook->gch_client_try_connect;
	gc_hook.gch_client_cancel_try_connect 
	    = hook->gch_client_cancel_try_connect;
	gc_hook.gch_post_init = hook->gch_post_init;
    }
}

/*
 * Returns client id set in call
 */
const char *
grpc_c_grpc_get_client_id (grpc_call *call)
{
    if (call && gc_hook.gch_get_client_id) {
	return gc_hook.gch_get_client_id(call);
    }
    return NULL;
}

/*
 * Returns underlying grpc type
 */
gc_grpc_type_t 
grpc_c_get_type () 
{
    return gc_hook.gch_type;
}

/*
 * Sets disconnect callback into transport
 */
void 
grpc_c_grpc_set_disconnect_cb (grpc_call *call, 
			       grpc_c_client_disconnect_callback_t *cb)
{
    if (call && gc_hook.gch_set_disconnect_cb) {
	gc_hook.gch_set_disconnect_cb(call, cb);
    }
}

/*
 * Sets client id into transport
 */
void 
grpc_c_grpc_set_client_id (grpc_call *call, const char *id)
{
    if (call && gc_hook.gch_set_client_id) {
	gc_hook.gch_set_client_id(call, id);
    }
}

/*
 * Sets evcontext into libgrpc layer
 */
void 
grpc_c_grpc_set_evcontext (void *evcontext)
{
    if (gc_hook.gch_set_evcontext) {
	gc_hook.gch_set_evcontext(evcontext);
    }
}

/*
 * Sets client task pointer into libgrpc layer
 */
void 
grpc_c_grpc_set_client_task (void *task)
{
    if (gc_hook.gch_set_client_task) {
	gc_hook.gch_set_client_task(task);
    }
}

/*
 * Sets callback on completion queue
 */
void 
grpc_c_grpc_set_cq_callback (grpc_completion_queue *cq, 
			     int (*cb)(grpc_completion_queue *cq))
{
    if (gc_hook.gch_set_cq_callback) {
	gc_hook.gch_set_cq_callback(cq, cb);
    }
}

/*
 * Sets callback that gets called when socket on client is created
 */
void 
grpc_c_grpc_set_client_socket_create_callback (void (*fp)(int fd, 
							  const char *uri))
{
    if (gc_hook.gch_set_client_socket_create_cb) {
	gc_hook.gch_set_client_socket_create_cb(fp);
    }
}

/*
 * Attempts to connect to server from client. Returns 0 on success
 */
int 
grpc_c_grpc_client_try_connect (long timeout, void (*timeout_cb)(void *data), 
				void *timeout_cb_arg, void **tag) {
    if (gc_hook.gch_client_try_connect) {
	return gc_hook.gch_client_try_connect(timeout, timeout_cb, 
					       timeout_cb_arg, tag);
    }
    return 1;
}

/*
 * Cancels a connection retry attempt to server from client
 */
void 
grpc_c_grpc_client_cancel_try_connect (void *closure) 
{
    if (gc_hook.gch_client_cancel_try_connect) {
	gc_hook.gch_client_cancel_try_connect(closure);
    }
}

/*
 * Returns threadpool
 */
grpc_c_thread_pool_t *
grpc_c_get_thread_pool ()
{
    return gc_tpool;
}

/*
 * Initializes japi library
 */
void 
grpc_c_init (gc_grpc_type_t type, void *data) 
{
    /*
     * Initialize trace functions
     */
    grpc_c_trace_init();

    /*
     * Initialize grpc
     */
    grpc_init();

    /*
     * Make sure our underlying library is the same as user expects
     */
    GPR_ASSERT(type == gc_hook.gch_type);

    /*
     * Set evcontext
     */
    if (type == GRPC_ISC) {
	grpc_c_grpc_set_evcontext(data);
    }

    /*
     * Create threads
     */
    if (type == GRPC_THREADS) {
	if (gc_tpool == NULL) {
	    if (data == NULL || *(int *)data <= 0) {
		gc_tpool = grpc_c_thread_pool_create(gpr_cpu_num_cores());
	    } else {
		gc_tpool = grpc_c_thread_pool_create(*(int *)data);
	    }

	    GPR_ASSERT(gc_tpool != NULL);
	}
    }

    /*
     * Invoke any post grpc init callback if available
     */
    if (gc_hook.gch_post_init) {
	gc_hook.gch_post_init();
    }
}

/*
 * Cleans up and shutsdown japi library
 */
void 
grpc_c_shutdown ()
{
    grpc_shutdown();
    if (gc_tpool) {
	grpc_c_thread_pool_shutdown(gc_tpool);
    }
}
