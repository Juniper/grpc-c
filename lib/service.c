/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdlib.h>

#include "config.h"
#include "common/aux_types.h"
#include "common/env_paths.h"
#include "common/strextra.h"
#include <grpc-c/grpc-c.h>

#include "context.h"
#include "hooks.h"
#include "stream_ops.h"
#include "thread_pool.h"
#include "trace.h"

/*
 * Forward declaration
 */
static int gc_handle_server_event (grpc_completion_queue *cq);
static void gc_schedule_callback (grpc_completion_queue *cq, 
				  grpc_c_server_t *server); 

/*
 * Opaque data used with grpc_c recv close event
 */
typedef struct gcs_recv_close_data_s {
    grpc_op op;
    grpc_c_context_t *context;
} gcs_recv_close_data_t;

/*
 * Data that gets passed to rpc listen thread
 */
struct gcs_thread_data_t {
    grpc_completion_queue *cq;
    grpc_c_server_t *server;
};

/*
 * Registers method callbacks
 */
static void
grpc_c_register_method_callbacks (grpc_c_server_t *server, const char *name,
				  grpc_c_service_callback_t *cb, 
				  grpc_c_method_data_pack_t *input_packer, 
				  grpc_c_method_data_unpack_t *input_unpacker, 
				  grpc_c_method_data_free_t *input_free, 
				  grpc_c_method_data_pack_t *output_packer, 
				  grpc_c_method_data_unpack_t *output_unpacker, 
				  grpc_c_method_data_free_t *output_free)
{
    server->gcs_method_funcs[server->gcs_method_count].gcmf_name 
	= gpr_strdup(name);
    server->gcs_method_funcs[server->gcs_method_count].gcmf_handler.gcmfh_server 
	= cb;
    server->gcs_method_funcs[server->gcs_method_count].gcmf_input_packer 
	= input_packer;
    server->gcs_method_funcs[server->gcs_method_count].gcmf_input_unpacker 
	= input_unpacker;
    server->gcs_method_funcs[server->gcs_method_count].gcmf_input_free 
	= input_free;
    server->gcs_method_funcs[server->gcs_method_count].gcmf_output_packer 
	= output_packer;
    server->gcs_method_funcs[server->gcs_method_count].gcmf_output_unpacker 
	= output_unpacker;
    server->gcs_method_funcs[server->gcs_method_count].gcmf_output_free 
	= output_free;

    server->gcs_method_count++;
}

/*
 * Registers a service method with its callback and data functions. Returns 1
 * on failure, 0 on success
 */
int 
grpc_c_register_method (grpc_c_server_t *server, const char *method, 
			int client_streaming, int server_streaming, 
			grpc_c_service_callback_t *handler, 
			grpc_c_method_data_pack_t *input_packer, 
			grpc_c_method_data_unpack_t *input_unpacker, 
			grpc_c_method_data_free_t *input_free, 
			grpc_c_method_data_pack_t *output_packer, 
			grpc_c_method_data_unpack_t *output_unpacker, 
			grpc_c_method_data_free_t *output_free)
{
    void *tag = grpc_server_register_method(server->gcs_server, method, 
					    server->gcs_host, 
					    GRPC_SRM_PAYLOAD_NONE, 0);
    if (tag == NULL) {
	gpr_log(GPR_ERROR, "Failed to register method %s", method);
	return 1;
    }

    struct grpc_c_method_t *gcm = gpr_malloc(sizeof(struct grpc_c_method_t));
    if (gcm == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory for method %s", method);
	return 1;
    }
    gcm->gcm_tag = tag;
    gcm->gcm_name = gpr_strdup(method);
    gcm->gcm_method_id = server->gcs_method_count;
    gcm->gcm_client_streaming = client_streaming;
    gcm->gcm_server_streaming = server_streaming;

    /*
     * Insert into the list of methods
     */
    LIST_INSERT_HEAD(&server->gcs_method_list_head, gcm, gcm_list);

    /*
     * Handler and data callbacks
     */
    grpc_c_register_method_callbacks(server, method, handler, input_packer, 
				     input_unpacker, input_free, output_packer, 
				     output_unpacker, output_free);

    return 0;
}

/*
 * Finishes read operations and clears payload buffer
 */
static int
gc_read_ops_finish (grpc_c_context_t *context, grpc_c_status_t *status UNUSED, 
		    uint32_t flags UNUSED)
{
    grpc_byte_buffer_destroy(context->gcc_payload);
    context->gcc_payload = NULL;

    return 0;
}

static int
gc_register_grpc_method (grpc_c_server_t *server, struct grpc_c_method_t *np) 
{
    grpc_call_error e;

    /*
     * Create a context that gets returned when this is method is called
     */
    grpc_c_context_t *context = grpc_c_context_init(np, 0);
    if (context == NULL) {
	gpr_log(GPR_ERROR, "Failed to create context before starting server");
	return 1;
    }

    context->gcc_event.gce_type = GRPC_C_EVENT_RPC_INIT;
    context->gcc_event.gce_data = context;

    context->gcc_cq = grpc_completion_queue_create(NULL);
    grpc_c_grpc_set_cq_callback(context->gcc_cq, gc_handle_server_event);
    context->gcc_data.gccd_server = server;
    context->gcc_state = GRPC_C_SERVER_CALLBACK_WAIT;

    server->gcs_contexts[np->gcm_method_id] = context;


    if (!server->gcs_shutdown) {
	e = grpc_server_request_registered_call(server->gcs_server, 
						np->gcm_tag, 
						&context->gcc_call, 
						&context->gcc_deadline, 
						context->gcc_metadata, 
						NULL, 
						context->gcc_cq, 
						server->gcs_cq, 
						&context->gcc_event);

	if (e != GRPC_CALL_OK) {
	    grpc_c_context_free(context);
	    gpr_log(GPR_ERROR, "Failed to register call: %d", e);
	    return 1;
	}
	context->gcc_event.gce_refcount++;
    }

    return 0;
}

/*
 * Reregisters a method once the RPC callback is finished so we can receive
 * next call for same RPC
 */
static int
gc_reregister_method (grpc_c_server_t *server, int method_id)
{
    struct grpc_c_method_t *np;

    for (np = server->gcs_method_list_head.lh_first; np != NULL; 
	 np = np->gcm_list.le_next) {
	if (np->gcm_method_id == method_id) break;
    }

    if (np && np->gcm_method_id == method_id) {
	return gc_register_grpc_method(server, np);
    }

    gpr_log(GPR_ERROR, "Failed to reregister method with id %d", method_id);
    return 1;
}

/*
 * Prepares context to be passed to RPC handler. Registers another request for 
 * this call before proceeding to call the service implementation
 */
static int
gc_prepare_server_callback (grpc_c_context_t *context)
{
    int rc;
    gcs_recv_close_data_t *gcs_rc_data;

    grpc_c_server_t *server = context->gcc_data.gccd_server;
    struct grpc_c_method_t *method = context->gcc_method;
    int method_id = method->gcm_method_id;

    grpc_c_stream_handler_t *stream_handler 
	= gpr_malloc(sizeof(grpc_c_stream_handler_t));
    if (stream_handler == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory for stream handler");
	grpc_c_context_free(context);
	return 1;
    }

    /*
     * Fill in packer/unpacker functions for input and output
     */
    if (context->gcc_data.gccd_server 
	&& context->gcc_data.gccd_server->gcs_method_funcs) {
	memcpy(context->gcc_method_funcs, 
	       &context->gcc_data.gccd_server->gcs_method_funcs[method_id], 
	       sizeof(grpc_c_method_funcs_t));
    }

    context->gcc_stream = stream_handler;

    stream_handler->read = &gc_stream_read;
    stream_handler->write = &gc_stream_write;
    stream_handler->finish = &gc_server_stream_finish;

    /*
     * Reregister the method so next call to this RPC can be caught
     */
    rc = gc_reregister_method(server, method_id);

    /*
     * Start a batch operation so we know when the call is cancelled either
     * because of client disconnection or deliberate client cancel. SAve this
     * grpc_c_event in context so we can query it if needed
     */
    grpc_c_event_t *gcev = &context->gcc_recv_close_event;
    gcs_rc_data = gpr_malloc(sizeof(gcs_recv_close_data_t));
    if (gcs_rc_data == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory for event data");
	return 1;
    }
    gcs_rc_data->op.op = GRPC_OP_RECV_CLOSE_ON_SERVER;
    gcs_rc_data->op.data.recv_close_on_server.cancelled 
	= &context->gcc_client_cancel;
    gcs_rc_data->op.flags = 0;
    gcs_rc_data->op.reserved = NULL;
    gcs_rc_data->context = context;

    gcev->gce_type = GRPC_C_EVENT_RECV_CLOSE;
    gcev->gce_data = gcs_rc_data;
    gcev->gce_refcount++;

    gpr_mu_lock(context->gcc_lock);
    grpc_call_error e = grpc_call_start_batch(context->gcc_call, 
					      &gcs_rc_data->op, 1, gcev, NULL);
    gpr_mu_unlock(context->gcc_lock);

    if (e != GRPC_CALL_OK) {
	gpr_log(GPR_ERROR, "Failed to start batch for RECV_CLOSE");
	gpr_free(gcs_rc_data);
	return 1;
    }

    /*
     * Call the service handler
     */
    server->gcs_method_funcs[method_id].gcmf_handler.gcmfh_server(context);

    return rc;
}

/*
 * Extracts client-id from metadata. Returns NULL if unavailable
 */
const char *
grpc_c_get_client_id (grpc_c_context_t *context) 
{
    const char *client_id = "";

    if (context && context->gcc_data.gccd_server) {
	/*
	 * Look for client-id in metadata and save it in channel args for
	 * future use
	 */
	client_id = grpc_c_get_metadata_by_key(context, "client-id");
    }

    return client_id ? client_id : "";
}

/*
 * Gets context from event and handles it depending on the state
 */
static void
gc_handle_server_complete_op (grpc_c_context_t *context, int success) 
{
    const char *client_id = grpc_c_get_client_id(context);

    /*
     * Set if disconnect callback is available and not already set
     */
    if (context->gcc_state != GRPC_C_SERVER_CONTEXT_NOOP && context->gcc_call 
	&& context->gcc_data.gccd_server->gcs_client_disconnect_cb) {
	grpc_c_grpc_set_disconnect_cb(context->gcc_call, context->gcc_data
				    .gccd_server->gcs_client_disconnect_cb);
    }

    if (context->gcc_state == GRPC_C_SERVER_CALLBACK_WAIT) {
	context->gcc_state = GRPC_C_SERVER_CALLBACK_START;

	/*
	 * If we have recevied a failed event even before we receive request
	 * for RPC, we clean the context and register for new request on this
	 * RPC
	 */
	if (success == 0) {
	    /*
	     * If we are not shutting down, register for next RPC
	     */
	    if (!context->gcc_data.gccd_server->gcs_shutdown) {
		gc_reregister_method(context->gcc_data.gccd_server, 
				     context->gcc_method->gcm_method_id);
	    }

	    /*
	     * Remove this context from server's context list and free right
	     * away
	     */
	    if (context && context->gcc_data.gccd_server) {
		grpc_c_server_t *server = context->gcc_data.gccd_server;
		int method_id = context->gcc_method->gcm_method_id;
		if (server->gcs_contexts[method_id] == context) {
		    server->gcs_contexts[method_id] = NULL;
		}
	    }
	    grpc_c_context_free(context);
	    return;
	}

	/*
	 * Check if we have connect callback registered and called. If
	 * not, this is the right time to call with client-id. We need to do
	 * this if we are using grpc with libisc2 or jtask
	 */
	if (context->gcc_call && client_id != NULL 
	    && grpc_c_get_type() > GRPC_THREADS) {
	    /*
	     * If client-id is not set in transport, we wouldn't have
	     * called connect callback
	     */
	    if (grpc_c_grpc_get_client_id(context->gcc_call) == NULL) {
		/*
		 * If connect callback is given, call it
		 */
		if (context->gcc_data.gccd_server->gcs_client_connect_cb) {
		    context->gcc_data
			.gccd_server->gcs_client_connect_cb(client_id);
		}

		/*
		 * Set client-id in transport
		 */
		grpc_c_grpc_set_client_id(context->gcc_call, client_id);
	    }
	}
	GPR_ASSERT(gc_prepare_server_callback(context) == 0);
    } else if (context->gcc_state == GRPC_C_SERVER_CONTEXT_CLEANUP) {
	/*
	 * We have successfully finished sending write finish ops, we can go
	 * ahead cleanup the context. Otherwise try resending write finish ops 
	 */
	if (success || context->gcc_call_cancelled) {
	    context->gcc_state == GRPC_C_SERVER_CONTEXT_NOOP;
	    grpc_c_server_t *server = context->gcc_data.gccd_server;
	    gpr_mu_lock(&server->gcs_lock);

	    /*
	     * Unref context from recv close event
	     */
	    if (context->gcc_recv_close_event.gce_data) {
		((gcs_recv_close_data_t *)
		 (context->gcc_recv_close_event.gce_data))->context = NULL;
	    }
	    grpc_c_context_free(context);
	    server->gcs_running_cb--;
	    if (gc_trace) {
		gpr_log(GPR_DEBUG, "Decrementing running callback %d\n", 
			server->gcs_running_cb);
	    }
	    gpr_mu_unlock(&server->gcs_lock);
	    /*
	     * If we are shutting down and finished all our callbacks, signal
	     * shutdown condition variable so we can finish destroying the server
	     */
	    if (server->gcs_running_cb == 0 && server->gcs_shutdown) {
		gpr_cv_signal(&server->gcs_shutdown_cv);
	    }
	} else {
	    grpc_c_status_t status;
	    status.gcs_code = context->gcc_status;
	    bzero(status.gcs_message, sizeof(status.gcs_message));
	    gc_server_stream_finish(context, &status, 0);
	}
    }
}

/*
 * Internal function that handles server events
 */
static int 
gc_handle_server_event_internal (grpc_completion_queue *cq, 
				 grpc_c_server_t *server, gpr_timespec ts)
{
    grpc_event ev;
    grpc_c_context_t *context = NULL;
    grpc_completion_queue *server_cq = cq;
    int shutdown = 0, timeout = 0, resolved = 0;
    gpr_mu *context_lock = NULL;
    int rc = 0;

    while (!shutdown && !timeout) {
	timeout = 0;
	grpc_c_state_t state = GRPC_C_SERVER_CALLBACK_WAIT;

	/*
	 * Access to context cq needs synchronized in rpc threads
	 */
	if (context_lock) {
	    gpr_mu_lock(context_lock);
	}
	ev = grpc_completion_queue_next(cq, ts, NULL);
	if (context_lock) {
	    gpr_mu_unlock(context_lock);
	}

	switch (ev.type) {
	    case GRPC_OP_COMPLETE:
		if (gc_trace) {
		    gpr_log(GPR_DEBUG, "Received completion op");
		}

		/*
		 * If this is a event to recv close from client, retrieve
		 * context and set cancelled flag in it
		 */
		grpc_c_event_t *gcev = (grpc_c_event_t *)ev.tag;
		if (gcev == NULL) break;

		if (gcev->gce_type == GRPC_C_EVENT_RECV_CLOSE) {
		    context = ((gcs_recv_close_data_t *)gcev->gce_data)->context;
		    if (context) {
			context->gcc_call_cancelled = 1;
		    }
		    gpr_free(gcev->gce_data);
		    gcev->gce_data = NULL;
		} else if (gcev->gce_type == GRPC_C_EVENT_READ) {
		    /*
		     * Our read has resolved. If the user has set a 
		     * callback, invoke it
		     */
		    context = (grpc_c_context_t *)gcev->gce_data;
		    if (context && context->gcc_read_resolve_cb) {
			(context->gcc_read_resolve_cb)
			    (context, context->gcc_read_resolve_arg, 
			     ev.success);
			context->gcc_read_resolve_cb = NULL;
		    }
		} else if (gcev->gce_type == GRPC_C_EVENT_WRITE) {
		    /*
		     * Our previous write has resolved. We can invoke user 
		     * provided callback so he can continue writing
		     */
		    context = (grpc_c_context_t *)gcev->gce_data;
		    if (context && context->gcc_write_resolve_cb) {
			(context->gcc_write_resolve_cb)
			    (context, context->gcc_write_resolve_arg, 
			     ev.success);
			context->gcc_write_resolve_cb = NULL;
		    }
		} else if (gcev->gce_type == GRPC_C_EVENT_RPC_INIT 
			   || gcev->gce_type == GRPC_C_EVENT_WRITE_FINISH) {
		    /*
		     * Prepare and invoke rpc callback
		     */
		    context = (grpc_c_context_t *)gcev->gce_data;
		    if (context == NULL 
			|| context->gcc_data.gccd_server == NULL) {
			gpr_log(GPR_ERROR, "Invalid context for rpc");
			break;
		    } else if (grpc_c_get_thread_pool() != NULL && ev.success) {
			/*
			 * If we are event/task based, we get the initial 
			 * complete event on server cq. Switch to call cq so 
			 * we can pull further rpc related events
			 */
			if (gcev->gce_type == GRPC_C_EVENT_WRITE_FINISH 
			    || ev.success) {
			    cq = context->gcc_cq;
			}
		    }

		    /*
		     * If we are threaded and our rpc just got resolved, 
		     * schedule call for next rpc before processing this
		     */
		    if (resolved == 0 && grpc_c_get_thread_pool() != NULL 
			&& !context->gcc_data.gccd_server->gcs_shutdown) {
			gc_schedule_callback(server_cq, server);
			resolved = 1;
		    }

		    state = context->gcc_state;
		    if (ev.success && state == GRPC_C_SERVER_CALLBACK_WAIT 
			&& !context->gcc_data.gccd_server->gcs_shutdown) {
			gpr_mu_lock(&context->gcc_data.gccd_server->gcs_lock);
			context->gcc_data.gccd_server->gcs_running_cb++;
			if (gc_trace) {
			    gpr_log(GPR_DEBUG, "Incrementing running "
				    "callbacks on server - %d", 
				    context->gcc_data.gccd_server->gcs_running_cb);
			}
			gpr_mu_unlock(&context->gcc_data.gccd_server->gcs_lock);

			/*
			 * Create a context lock to syncronize access to 
			 * this cq
			 */
			context->gcc_lock = gpr_malloc(sizeof(gpr_mu));
			if (context->gcc_lock == NULL) {
			    gpr_log(GPR_ERROR, "Failed to allocate context lock");
			    break;
			}
			gpr_mu_init(context->gcc_lock);
			context_lock = context->gcc_lock;
		    }

		    /*
		     * Handle complete op
		     */
		    gc_handle_server_complete_op(context, ev.success);
		} else if (gcev->gce_type == GRPC_C_EVENT_SERVER_SHUTDOWN) {
		    gpr_log(GPR_DEBUG, "Server shutdown complete");
		}
		if (context && context->gcc_lock) gpr_mu_lock(context->gcc_lock);
		gcev->gce_refcount--;
		if (context && context->gcc_lock) gpr_mu_unlock(context->gcc_lock);
		break;
	    case GRPC_QUEUE_SHUTDOWN:
		grpc_completion_queue_destroy(cq);
		/*
		 * If we are threaded and we got shutdown event on server cq,
		 * destroy and notify gcs_cq_desctroy_cv so grpc_c_server_wait
		 * can proceed to destroy server and shutdown grpc
		 */
		gpr_log(GPR_DEBUG, "We have a shutdown event here %p", cq);
		if (grpc_c_get_thread_pool() != NULL && server 
		    && server->gcs_cq == cq) {
		    gpr_log(GPR_DEBUG, "We have a server shutdown event here %p", cq);
		    gpr_mu_lock(&server->gcs_lock);
		    server->gcs_cq_shutdown = 1;
		    gpr_cv_broadcast(&server->gcs_cq_destroy_cv);
		    gpr_mu_unlock(&server->gcs_lock);
		}

		/*
		 * Destroy lock from context
		 */
		if (context_lock && server && server->gcs_cq != cq) {
		    gpr_mu_destroy(context_lock);
		    gpr_free(context_lock);
		    context_lock = NULL;
		}
		shutdown = 1;
		break;
	    case GRPC_QUEUE_TIMEOUT:
		if (gc_trace) {
		    gpr_log(GPR_DEBUG, "Received timeout op on cq");
		}
		timeout = 1;
		break;
	    default:
		if (gc_trace) {
		    gpr_log(GPR_INFO, "Unknown event");
		}
		timeout = 1;
		rc = -1;
	}
    }
    return rc;
}

/*
 * Waits out for an RPC in thread
 */
static void 
gc_run_rpc (void *arg) 
{
    struct gcs_thread_data_t *data = (struct gcs_thread_data_t *)arg;
    grpc_completion_queue *cq = data->cq;
    grpc_c_server_t *server = data->server;
    gpr_free(data);

    gc_handle_server_event_internal(cq, server, 
				    gpr_inf_future(GPR_CLOCK_REALTIME));
}

/*
 * Waits for and takes action on incoming RPC requests
 */
static void 
gc_schedule_callback (grpc_completion_queue *cq, grpc_c_server_t *server) 
{
    struct gcs_thread_data_t *data = gpr_malloc(sizeof(struct gcs_thread_data_t));
    if (data == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memeory for thread");
	return;
    }
    data->cq = cq;
    data->server = server;

    grpc_c_thread_pool_add(grpc_c_get_thread_pool(), gc_run_rpc, (void *)data);
}

/*
 * Handler function that gets called whenever a event is available
 */
static int
gc_handle_server_event (grpc_completion_queue *cq)
{
    return gc_handle_server_event_internal(cq, NULL,  
					   gpr_inf_past(GPR_CLOCK_REALTIME));
}

/*
 * Wait for callback execution on server
 */
void
grpc_c_server_wait (grpc_c_server_t *server) 
{
    gpr_mu mu;
    gpr_cv *callback_cv = NULL;
    int running_cb = 0, shutdown = 0;

    callback_cv = gpr_malloc(sizeof(gpr_cv));
    if (callback_cv == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory for callback cv");
	return;
    }

    server->gcs_callback_cv = callback_cv;
    server->gcs_callback_running_cb = &running_cb;
    server->gcs_callback_shutdown = &shutdown;

    gpr_cv_init(callback_cv);
    gpr_mu_init(&mu);
    gpr_mu_lock(&mu);
    while (running_cb > 0 || !shutdown) {
	gpr_cv_wait(callback_cv, &mu, 
		    gpr_inf_future(GPR_CLOCK_REALTIME));
    }
    gpr_mu_unlock(&mu);
    gpr_cv_destroy(callback_cv);
    gpr_mu_destroy(&mu);
    gpr_free(callback_cv);
}

/*
 * Iterates through the list of registered methods requesting for RPC
 * invocations
 */
int
grpc_c_server_start (grpc_c_server_t *server)
{
    struct grpc_c_method_t *np;

    if (server == NULL) {
	gpr_log(GPR_ERROR, "Invalid server");
	return 1;
    }

    grpc_server_start(server->gcs_server);

    for (np = server->gcs_method_list_head.lh_first; np != NULL; 
	 np = np->gcm_list.le_next) {
	if (gc_register_grpc_method(server, np)) return 1;
    }

    /*
     * Schedule a callback if we are threaded
     */
    if (grpc_c_get_thread_pool()) {
	gc_schedule_callback(server->gcs_cq, server);
    }

    return 0;
}

/*
 * Allocates memory requried for methods
 */
int
grpc_c_methods_alloc (grpc_c_server_t *server, int method_count)
{
    if (server->gcs_method_funcs == NULL) {
	server->gcs_method_funcs = gpr_malloc(method_count 
					      * sizeof(grpc_c_method_funcs_t));
	server->gcs_contexts = gpr_malloc(method_count 
					  * sizeof(grpc_c_context_t *));
    } else {
	server->gcs_method_funcs = gpr_realloc(server->gcs_method_funcs, 
					       (method_count + server->gcs_method_count) 
					       * sizeof(grpc_c_method_funcs_t));
	server->gcs_contexts = gpr_realloc(server->gcs_contexts, 
					   (method_count + server->gcs_method_count) 
					   * sizeof(grpc_c_context_t *));
    }

    if (server->gcs_method_funcs == NULL || server->gcs_contexts == NULL) {
	gpr_free(server->gcs_method_funcs);
	gpr_free(server->gcs_contexts);
	return 1;
    }
    
    return 0;
}

/*
 * Registers connect callback that will called when tcp connection is
 * established from client
 */
void 
grpc_c_register_connect_callback (grpc_c_server_t *server, 
				  grpc_c_client_connect_callback_t *cb)
{
    if (server && cb) {
	server->gcs_client_connect_cb = cb;
    }
}

/*
 * Regsiters disconnect callback that gets called when client closes the
 * connection
 */
void 
grpc_c_register_disconnect_callback (grpc_c_server_t *server, 
				     grpc_c_client_disconnect_callback_t *cb)
{
    if (server && cb) {
	server->gcs_client_disconnect_cb = cb;
    }
}

/*
 * Creates a grpc server for given host
 */
static grpc_c_server_t *
gc_server_create_internal (const char *host, grpc_server_credentials *creds, 
			   grpc_channel_args *args)
{
    /*
     * Server structure stuff
     */
    grpc_c_server_t *server = gpr_malloc(sizeof(grpc_c_server_t));
    if (server == NULL) {
	return NULL;
    }
    memset(server, 0, sizeof(grpc_c_server_t));

    server->gcs_cq = grpc_completion_queue_create(NULL);
    grpc_c_grpc_set_cq_callback(server->gcs_cq, gc_handle_server_event);
    server->gcs_server = grpc_server_create(args, NULL);
    server->gcs_host = strdup(host);
    server->gcs_shutdown_event.gce_type = GRPC_C_EVENT_SERVER_SHUTDOWN;
    gpr_mu_init(&server->gcs_lock);
    gpr_cv_init(&server->gcs_cq_destroy_cv);
    gpr_cv_init(&server->gcs_shutdown_cv);

    /*
     * If we have credentials, we create a secure server
     */
    if (creds) {
	if (grpc_server_add_secure_http2_port(server->gcs_server, host, 
					      creds) == 0) {
	    grpc_c_server_destroy(server);
	    return NULL;
	}
    } else {
	if (grpc_server_add_insecure_http2_port(server->gcs_server, 
						host) == 0) {
	    grpc_c_server_destroy(server);
	    return NULL;
	}
    }
    grpc_server_register_completion_queue(server->gcs_server, server->gcs_cq, 
					  NULL);
    LIST_INIT(&server->gcs_method_list_head);

    return server;
}

/*
 * Creates a grpc-c server with provided name
 */
grpc_c_server_t *
grpc_c_server_create (const char *name, grpc_server_credentials *creds, 
		      grpc_channel_args *args)
{
    char buf[BUFSIZ];

    if (name == NULL) {
	return NULL;
    }

    snprintf_safe(buf, sizeof(buf), "%s%s", PATH_GRPC_C_DAEMON_SOCK, name);

    if (buf[0] == '\0') {
	return NULL;
    }
    
    return gc_server_create_internal(buf, creds, args);
}

/*
 * Creates a grpc-c server with provided address
 */
grpc_c_server_t *
grpc_c_server_create_by_host (const char *addr, grpc_server_credentials *creds, 
			      grpc_channel_args *args)
{
    if (addr == NULL) {
	return NULL;
    }

    return gc_server_create_internal(addr, creds, args);
}

/*
 * Adds insecure ip/port to grpc server
 */
int
grpc_c_server_add_insecure_http2_port (grpc_c_server_t *server, 
				       const char* addr) 
{
    if (server == NULL) return 0;

    return grpc_server_add_insecure_http2_port(server->gcs_server, addr);
}

/*
 * Adds secure ip/port to grpc server
 */
int 
grpc_c_server_add_secure_http2_port (grpc_c_server_t *server, 
				     const char *addr, 
				     grpc_server_credentials *creds)
{
    if (server == NULL) return 0;

    return grpc_server_add_secure_http2_port(server->gcs_server, addr, creds);
}

/*
 * Checks if the client corresponding to this context on other end of the
 * channel has cancelled. This must be called on a valid context. Returns 1 if
 * cancelled and 0 otherwise
 */
int 
grpc_c_context_is_call_cancelled (grpc_c_context_t *context)
{
    grpc_event ev;
    gpr_timespec deadline = gpr_time_0(GPR_CLOCK_REALTIME);

    if (context == NULL) return 1;

    /*
     * This is invalid for client contexts. Instead they should check gcc_call
     */
    if (context->gcc_is_client) return 0;

    gpr_mu_lock(context->gcc_lock);
    ev = grpc_completion_queue_pluck(context->gcc_cq, 
				     &context->gcc_recv_close_event, deadline, 
				     NULL);
    gpr_mu_unlock(context->gcc_lock);

    if (ev.type == GRPC_OP_COMPLETE) {
	context->gcc_call_cancelled = 1;
	gpr_free(context->gcc_recv_close_event.gce_data);
	context->gcc_recv_close_event.gce_data = NULL;
	return ev.success;
    }

    return 0;
}

/*
 * Shutsdown and releases server
 */
void 
grpc_c_server_destroy (grpc_c_server_t *server)
{
    int i;
    struct grpc_c_method_t *np;

    if (server != NULL) {
	/*
	 * Wait till all the current running callbacks finish to completion.
	 * Mark server as shutdown so we don't accept any new jobs
	 */
	if (grpc_c_get_thread_pool()) {
	    gpr_mu_lock(&server->gcs_lock);
	    server->gcs_shutdown = 1;
	    while (server->gcs_running_cb > 0) {
		gpr_cv_wait(&server->gcs_shutdown_cv, &server->gcs_lock, 
			    gpr_inf_future(GPR_CLOCK_REALTIME));
	    }
	    gpr_mu_unlock(&server->gcs_lock);
	}

	if (server->gcs_host) gpr_free(server->gcs_host);

	if (server->gcs_server) {
	    server->gcs_shutdown_event.gce_refcount++;
	    grpc_server_shutdown_and_notify(server->gcs_server, 
					    server->gcs_cq, 
					    &server->gcs_shutdown_event);
	}

	if (server->gcs_cq) {
	    grpc_completion_queue_shutdown(server->gcs_cq);
	    if (!grpc_c_get_thread_pool()) {
		while (grpc_completion_queue_next(server->gcs_cq, 
						  gpr_inf_past(GPR_CLOCK_REALTIME), 
						  NULL).type != GRPC_QUEUE_SHUTDOWN)
		    ;

		if (server->gcs_server) {
		    grpc_server_destroy(server->gcs_server);
		}
		grpc_completion_queue_destroy(server->gcs_cq);
	    } else if (server->gcs_server) {
		gpr_log(GPR_DEBUG, "Waiting for server destroy cv");
		gpr_mu_lock(&server->gcs_lock);
		while (server->gcs_cq_shutdown == 0) {
		    gpr_cv_wait(&server->gcs_cq_destroy_cv, &server->gcs_lock, 
				gpr_inf_future(GPR_CLOCK_REALTIME));
		}
		grpc_server_destroy(server->gcs_server);
		gpr_mu_unlock(&server->gcs_lock);
	    }
	}

	/*
	 * Free pending contexts and method funcs
	 */
	if (server->gcs_method_funcs) {
	    for (i = 0; i < server->gcs_method_count; i++) {
		gpr_free(server->gcs_method_funcs[i].gcmf_name);
		if (server->gcs_contexts[i]) {
		    grpc_completion_queue_shutdown(server->gcs_contexts[i]->gcc_cq);
		    grpc_completion_queue_destroy(server->gcs_contexts[i]->gcc_cq);
		    grpc_c_context_free(server->gcs_contexts[i]);
		}
	    }
	    gpr_free(server->gcs_method_funcs);
	    gpr_free(server->gcs_contexts);
	}

	while (!LIST_EMPTY(&server->gcs_method_list_head)) {
	    np = LIST_FIRST(&server->gcs_method_list_head);
	    LIST_REMOVE(np, gcm_list);

	    if (np->gcm_name) gpr_free(np->gcm_name);

	    gpr_free(np);
	}

	/*
	 * Update running cb and shutdown values so grpc_c_server_wait() can
	 * come out
	 */
	if (server->gcs_callback_running_cb) {
	    *server->gcs_callback_running_cb = server->gcs_running_cb;
	}

	if (server->gcs_callback_shutdown) {
	    *server->gcs_callback_shutdown = server->gcs_shutdown;
	}
	
	gpr_cv_broadcast(server->gcs_callback_cv);
	gpr_cv_destroy(&server->gcs_shutdown_cv);
	gpr_cv_destroy(&server->gcs_cq_destroy_cv);
	gpr_mu_destroy(&server->gcs_lock);

	gpr_free(server);
    }
}
