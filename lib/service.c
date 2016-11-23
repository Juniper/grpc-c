/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "common/aux_types.h"
#include "common/env_paths.h"
#include "common/strextra.h"
#include <grpc-c/grpc-c.h>

#include "context.h"
#include "thread_pool.h"
#include "hooks.h"

/*
 * Call structure to access completion queue and channel
 */
typedef struct {
    grpc_completion_queue *cq;
    grpc_channel *channel;
} grpc_call_t;

/*
 * Forward declaration
 */
static int gc_handle_server_event (grpc_completion_queue *cq);
static void gc_schedule_callback (grpc_completion_queue *cq); 

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
    server->gcs_method_funcs[server->gcs_method_count].gcmf_name = strdup(name);
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
					    GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER, 
					    0);
    if (tag == NULL) {
	gpr_log(GPR_ERROR, "Failed to register method %s", method);
	return 1;
    }

    struct grpc_c_method_t *gcm = malloc(sizeof(struct grpc_c_method_t));
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
 * Reads data from client into content and returns 0 if success. 
 * Returns 1 if there is no more data or a failure
 */
static int
gc_read_ops (grpc_c_context_t *context, void **content)
{
    int op_count = context->gcc_op_count;
    int method_id = context->gcc_method->gcm_method_id;

    /*
     * Check if we have pending optional payload
     */
    if (context->gcc_payload == NULL) {
	/*
	 * Make room for new ops
	 */
	if (grpc_c_ops_alloc(context, 1)) return 1;

	context->gcc_ops[op_count].op = GRPC_OP_RECV_MESSAGE;
	context->gcc_ops[op_count].data.recv_message = &context->gcc_payload;

	grpc_call_error e =  grpc_call_start_batch(context->gcc_call, 
						   context->gcc_ops, 
						   1, context, NULL);
	if (e != GRPC_CALL_OK) {
	    gpr_log(GPR_ERROR, "Failed to finish read ops batch");
	    return 1;
	}

	context->gcc_op_count = 0;
    }

    /*
     * Unpack payload to content to be read by user
     */
    *content = context->gcc_data.gccd_server->gcs_method_funcs[method_id]
	.gcmf_input_unpacker(context, context->gcc_payload);

    if (context->gcc_payload != NULL) {
	grpc_byte_buffer_destroy(context->gcc_payload);
	context->gcc_payload = NULL;
	return 0;
    } else {
	return 1;
    }
}

/*
 * Writes the data given in output. Returns 0 if success. 1 if there is an
 * error
 */
static int
gc_write_ops (grpc_c_context_t *context, void *output, int batch)
{
    int op_count = context->gcc_op_count;
    int method_id = context->gcc_method->gcm_method_id;
    grpc_event ev;

    /*
     * If there is a pending write, return early
     */
    if (context->gcc_state == GRPC_C_WRITE_DATA_START) {
	return GRPC_C_WRITE_PENDING;
    }

    /*
     * Send initial metadata if we haven't already sent it
     */
    if (context->gcc_meta_sent == 0) {
	if (grpc_c_ops_alloc(context, 2)) return GRPC_C_WRITE_FAIL;

	context->gcc_ops[op_count].op = GRPC_OP_SEND_INITIAL_METADATA;
	context->gcc_ops[op_count].data.send_initial_metadata.count = 0;

	context->gcc_meta_sent = 1;
	context->gcc_op_count++;
    } else {
	if (grpc_c_ops_alloc(context, 1)) return 1;
    }

    /*
     * Convert to wire format and put it in buffer
     */

    /*
     * Destroy any previous allocated buffer
     */
    if (context->gcc_ops_payload[op_count] != NULL) {
	grpc_byte_buffer_destroy(context->gcc_ops_payload[op_count]);
    }
    context->gcc_data.gccd_server->gcs_method_funcs[method_id]
	.gcmf_output_packer(output, &context->gcc_ops_payload[op_count]);

    context->gcc_ops[context->gcc_op_count].op = GRPC_OP_SEND_MESSAGE;
    context->gcc_ops[context->gcc_op_count].data.send_message 
	= context->gcc_ops_payload[op_count];

    context->gcc_op_count++;

    if (batch == 0) {
	context->gcc_state = GRPC_C_WRITE_DATA_START;
	grpc_call_error e = grpc_call_start_batch(context->gcc_call, 
						  context->gcc_ops,
						  context->gcc_op_count, 
						  context, NULL);
	if (e == GRPC_CALL_OK) {
	    context->gcc_op_count = 0;
	} else {
	    gpr_log(GPR_ERROR, "Failed to finish batch operations to write data"
		    " - %d", e);
	    return GRPC_C_WRITE_FAIL;
	}

	if (grpc_c_get_thread_pool()) {
	    ev = grpc_completion_queue_pluck(context->gcc_cq, context, 
					     gpr_inf_future(GPR_CLOCK_REALTIME), 
					     NULL);
	} else {
	    ev = grpc_completion_queue_pluck(context->gcc_cq, context, 
					     gpr_inf_past(GPR_CLOCK_REALTIME), 
					     NULL);
	}
	if (ev.type == GRPC_OP_COMPLETE) {
	    context->gcc_state = GRPC_C_WRITE_DATA_DONE;
	    return GRPC_C_WRITE_OK;
	} else if (ev.type == GRPC_QUEUE_TIMEOUT) {
	    /*
	     * Our opertion is still pending. We should tell user about the
	     * same and give him a chance to register for a callback that gets
	     * called once this operation is finished
	     */
	    return GRPC_C_WRITE_PENDING;
	} else {
	    return GRPC_C_WRITE_FAIL;
	}
    }

    return GRPC_C_WRITE_OK;
}

/*
 * Finishes read operations and clears payload buffer
 */
static int
gc_read_ops_finish (grpc_c_context_t *context, grpc_c_status_t *status UNUSED)
{
    grpc_byte_buffer_destroy(context->gcc_payload);
    context->gcc_payload = NULL;

    return 0;
}

/*
 * Fills context with ops to send data and status. ignore_stream is a boolean
 * used to ignore if server is non-streaming and send message anyway
 */
static int
gc_write_ops_finish_internal (grpc_c_context_t *context, int status, 
			      int ignore_stream, const char *msg)
{
    int was_cancelled = 0;
    int op_count = context->gcc_op_count;
    grpc_byte_buffer *buf = grpc_raw_byte_buffer_create(NULL, 0);

    /*
     * If initial metadata is not sent, send inital metadata before sending 
     * close
     */
    if (context->gcc_meta_sent == 0) {
	if (grpc_c_ops_alloc(context, 1)) {
	    grpc_byte_buffer_destroy(buf);
	    gpr_log(GPR_ERROR, "Failed to allocate memory for ops");
	    return 1;
	}
	context->gcc_ops[op_count].op = GRPC_OP_SEND_INITIAL_METADATA;
	context->gcc_ops[op_count].data.send_initial_metadata.count = 0;

	context->gcc_meta_sent = 1;
	context->gcc_op_count++;
    }

    /*
     * If our server is streaming output, we send empty message before we
     * write status to indicate end of stream
     */
    if (ignore_stream || context->gcc_method->gcm_server_streaming) {
	if (grpc_c_ops_alloc(context, 3)) {
	    grpc_byte_buffer_destroy(buf);
	    gpr_log(GPR_ERROR, "Failed to allocate memory for ops");
	    return 1;
	};

	context->gcc_ops[context->gcc_op_count].op = GRPC_OP_SEND_MESSAGE;
	context->gcc_ops[context->gcc_op_count].data.send_message 
	    = buf;
	context->gcc_op_count++;
    } else {
	if (grpc_c_ops_alloc(context, 2)) {
	    grpc_byte_buffer_destroy(buf);
	    gpr_log(GPR_ERROR, "Failed to allocate memory for ops");
	    return 1;
	};
    }

    context->gcc_ops[context->gcc_op_count].op = GRPC_OP_SEND_STATUS_FROM_SERVER;

    /*
     * Return code of anything other than 0 will be sent as unknown error code
     */
    context->gcc_ops[context->gcc_op_count].data.send_status_from_server.status 
	= (status == 0) ? GRPC_STATUS_OK : GRPC_STATUS_UNKNOWN;
    context->gcc_ops[context->gcc_op_count].data.send_status_from_server
	.trailing_metadata_count = 0;
    context->gcc_ops[context->gcc_op_count].data.send_status_from_server
	.status_details = (msg != NULL) ? msg : "";
    context->gcc_op_count++;

    context->gcc_ops[context->gcc_op_count].op = GRPC_OP_RECV_CLOSE_ON_SERVER;
    context->gcc_ops[context->gcc_op_count].data.recv_close_on_server.cancelled 
	= &was_cancelled;

    context->gcc_op_count++;

    grpc_call_error e = grpc_call_start_batch(context->gcc_call, 
					      context->gcc_ops, 
					      context->gcc_op_count - op_count, 
					      context, NULL);

    context->gcc_state = GRPC_C_SERVER_CONTEXT_CLEANUP;
    if (e == GRPC_CALL_OK) {
	context->gcc_op_count = 0;
	grpc_byte_buffer_destroy(buf);
	return 0;
    } else {
	grpc_byte_buffer_destroy(buf);
	gpr_log(GPR_ERROR, "Failed to finish write batch ops");
	return 1;
    }
}

/*
 * Finishes write operations with an empty message and a status code. Returns
 * 0 on success. 1 on failure
 */
static int
gc_write_ops_finish (grpc_c_context_t *context, int status, const char *msg)
{
    return gc_write_ops_finish_internal(context, status, 0, msg);
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
						&context->gcc_payload, 
						context->gcc_cq, 
						server->gcs_cq, context);

	if (e != GRPC_CALL_OK) {
	    grpc_c_context_free(context);
	    gpr_log(GPR_ERROR, "Failed to register call: %d", e);
	    return 1;
	}
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
    grpc_c_server_t *server = context->gcc_data.gccd_server;
    struct grpc_c_method_t *method = context->gcc_method;
    int method_id = method->gcm_method_id;

    grpc_c_read_handler_t *read_handler = malloc(sizeof(grpc_c_read_handler_t));
    grpc_c_write_handler_t *write_handler 
	= malloc(sizeof(grpc_c_write_handler_t));

    context->gcc_reader = read_handler;
    context->gcc_writer = write_handler;

    if (read_handler == NULL || write_handler == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory for read/write handlers");
	grpc_c_context_free(context);
	return 1;
    }

    read_handler->read = &gc_read_ops;
    read_handler->finish = &gc_read_ops_finish;
    read_handler->free = server->gcs_method_funcs[method_id].gcmf_input_free;

    write_handler->write = &gc_write_ops;
    write_handler->finish = &gc_write_ops_finish;
    write_handler->free = server->gcs_method_funcs[method_id].gcmf_output_free;

    /*
     * Reregister the method so next call to this RPC can be caught
     */
    rc = gc_reregister_method(server, method_id);

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
grpc_c_get_client_id (grpc_c_context_t *context) {
    const char *client_id = "";
    size_t i = 0;

    if (context && context->gcc_data.gccd_server 
	&& context->gcc_metadata->count > 0) {

	/*
	 * Look for client-id in metadata and save it in channel args for
	 * future use
	 */
	for (i = 0; i < context->gcc_metadata->count; i++) {
	    if (streq(context->gcc_metadata->metadata[i].key, "client-id")) {
		client_id = context->gcc_metadata->metadata[i].value;
		break;
	    }
	}
    }

    return client_id;
}

/*
 * Gets context from event and handles it depending on the state
 */
static void
gc_handle_server_complete_op (grpc_c_context_t *context) 
{
    const char *client_id;

    /*
     * Set if disconnect callback is available and not already set
     */
    if (context->gcc_state != GRPC_C_SERVER_CONTEXT_NOOP && context->gcc_call 
	&& context->gcc_data.gccd_server->gcs_client_disconnect_cb) {
	grpc_c_grpc_set_disconnect_cb(((grpc_call_t *)context->gcc_call)->channel, 
				    context->gcc_data
				    .gccd_server->gcs_client_disconnect_cb);
    }

    if (context->gcc_state == GRPC_C_SERVER_CALLBACK_WAIT) {
	context->gcc_state = GRPC_C_SERVER_CALLBACK_START;

	/*
	 * If we mandate client-id in each call, make sure client
	 * sends it. Otherwise, return with 1 and don't bother the
	 * server. And reregister the method so we can listen for
	 * other calls
	 */
	client_id = grpc_c_get_client_id(context);
	if (client_id == NULL) {
	    gc_write_ops_finish_internal(context, 1, 1, NULL);
	    gc_reregister_method(context->gcc_data.gccd_server, 
				 context->gcc_method->gcm_method_id);
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
	    if (grpc_c_get_client_id_from_channel(((grpc_call_t *)
						   context->gcc_call)->channel) 
		== NULL) {

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
		grpc_c_grpc_set_client_id(((grpc_call_t *)context->gcc_call)->channel, 
					  client_id);
	    }
	}
	GPR_ASSERT(gc_prepare_server_callback(context) == 0);
    } else if (context->gcc_state == GRPC_C_SERVER_CONTEXT_CLEANUP) {
	context->gcc_state = GRPC_C_SERVER_CONTEXT_NOOP;
	grpc_c_server_t *server = context->gcc_data.gccd_server;
	gpr_mu_lock(&server->gcs_lock);
	grpc_c_context_free(context);
	server->gcs_running_cb--;
	gpr_mu_unlock(&server->gcs_lock);
	
	/*
	 * If we are shutting down and finished all our callbacks, signal
	 * shutdown condition variable so we can finish destroying the server
	 */
	if (server->gcs_running_cb == 0 && server->gcs_shutdown) {
	    gpr_cv_signal(&server->gcs_shutdown_cv);
	}
    } else if (context->gcc_state == GRPC_C_WRITE_DATA_START) {
	/*
	 * Our previous write was pending and is finished now. Call
	 * registered user callback if he registered one
	 */
	context->gcc_state = GRPC_C_WRITE_DATA_DONE;
	if (context->gcc_writer_resolve_cb) {
	    grpc_c_writer_resolve_callback_t *cb 
		= context->gcc_writer_resolve_cb;
	    void *cb_args = context->gcc_writer_resolve_args;

	    context->gcc_writer_resolve_cb = NULL;
	    context->gcc_writer_resolve_args = NULL;

	    cb(context, cb_args);
	}
    }
}

/*
 * Internal function that handles server events
 */
static int 
gc_handle_server_event_internal (grpc_completion_queue *cq, 
				 gpr_timespec ts)
{
    grpc_event ev;
    grpc_c_context_t *context = NULL;
    grpc_completion_queue *server_cq = cq;
    int shutdown = 0, timeout = 0, resolved = 0;
    int rc = 0;

    while (!shutdown && !timeout) {
	timeout = 0;
	grpc_c_state_t state = GRPC_C_SERVER_CALLBACK_WAIT;
	ev = grpc_completion_queue_next(cq, ts, NULL);

	switch (ev.type) {
	    case GRPC_OP_COMPLETE:
		/*
		 * Call the complete operation handler
		 */
		context = (grpc_c_context_t *)ev.tag;
		if (context == NULL) {
		    break;
		} else {
		    cq = context->gcc_cq;
		}

		/*
		 * If we are threaded and our rpc just got resolved, schedule
		 * call for next rpc before processing this
		 */
		if (resolved == 0 && grpc_c_get_thread_pool() != NULL 
		    && !context->gcc_data.gccd_server->gcs_shutdown) {
		    gc_schedule_callback(server_cq);
		    resolved = 1;
		}

		state = context->gcc_state;
		if (state == GRPC_C_SERVER_CALLBACK_WAIT 
		    && !context->gcc_data.gccd_server->gcs_shutdown 
		    && ev.success == 1) {
		    gpr_mu_lock(&context->gcc_data.gccd_server->gcs_lock);
		    context->gcc_data.gccd_server->gcs_running_cb++;
		    gpr_mu_unlock(&context->gcc_data.gccd_server->gcs_lock);
		}

		/*
		 * If we just resolved because of server shutdown, don't
		 * handle the RPC
		 */
		if (!(state == GRPC_C_SERVER_CALLBACK_WAIT 
		    && context->gcc_data.gccd_server->gcs_shutdown 
		    && ev.success == 1)) {
		    gc_handle_server_complete_op(context);
		}
		break;
	    case GRPC_QUEUE_SHUTDOWN:
		/*
		 * Destroy if we are not server completion queue. Server's
		 * completion queue will gets shutdown in
		 * japi_server_destroy()
		 */
		if (!grpc_c_get_thread_pool() || server_cq != cq) {
		    grpc_completion_queue_destroy(cq);
		}
		shutdown = 1;
		break;
	    case GRPC_QUEUE_TIMEOUT:
		timeout = 1;
		break;
	    default:
		gpr_log(GPR_INFO, "Unknown event");
		timeout = 1;
		rc = -1;
	}

	/*
	 * If we have this event from a server completion_queue, timeout after
	 * processing this event so the other server jobs get a chance to be
	 * processed when we are not threaded
	 */
	if (state == GRPC_C_SERVER_CALLBACK_WAIT && !timeout && !shutdown 
	    && grpc_c_get_type() > GRPC_THREADS) {
	    timeout = 1;
	    rc = 1;
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
    grpc_completion_queue *cq = (grpc_completion_queue *) arg;

    gc_handle_server_event_internal(cq, gpr_inf_future(GPR_CLOCK_REALTIME));
}

/*
 * Waits for and takes action on incoming RPC requests
 */
static void 
gc_schedule_callback (grpc_completion_queue *cq) 
{
    grpc_c_thread_pool_add(grpc_c_get_thread_pool(), gc_run_rpc, (void *)cq);
}

/*
 * Handler function that gets called whenever a event is available
 */
static int
gc_handle_server_event (grpc_completion_queue *cq)
{
    return gc_handle_server_event_internal(cq, 
					   gpr_inf_past(GPR_CLOCK_REALTIME));
}

/*
 * Wait for callback execution on server
 */
void
grpc_c_server_wait (grpc_c_server_t *server) 
{
    gpr_mu mu;

    gpr_cv_init(&server->gcs_callback_cv);
    gpr_mu_init(&mu);
    gpr_mu_lock(&mu);
    while (server->gcs_running_cb > 0 || !server->gcs_shutdown) {
	gpr_cv_wait(&server->gcs_callback_cv, &mu, 
		    gpr_inf_future(GPR_CLOCK_REALTIME));
    }
    gpr_mu_unlock(&mu);
    gpr_cv_destroy(&server->gcs_callback_cv);
    gpr_mu_destroy(&mu);
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
	gc_schedule_callback(server->gcs_cq);
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
	server->gcs_method_funcs = malloc(method_count 
					  * sizeof(grpc_c_method_funcs_t));
	server->gcs_contexts = malloc(method_count 
				      * sizeof(grpc_c_context_t *));
    } else {
	server->gcs_method_funcs = realloc(server->gcs_method_funcs, 
					   (method_count + server->gcs_method_count) 
					   * sizeof(grpc_c_method_funcs_t));
	server->gcs_contexts = realloc(server->gcs_contexts, 
				       (method_count + server->gcs_method_count) 
				       * sizeof(grpc_c_context_t *));
    }

    if (server->gcs_method_funcs == NULL || server->gcs_contexts == NULL) {
	free(server->gcs_method_funcs);
	free(server->gcs_contexts);
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
gc_server_create_internal (const char *host)
{
    /*
     * Server structure stuff
     */
    grpc_c_server_t *server = malloc(sizeof(grpc_c_server_t));
    if (server == NULL) {
	return NULL;
    }
    memset(server, 0, sizeof(grpc_c_server_t));

    server->gcs_cq = grpc_completion_queue_create(NULL);
    grpc_c_grpc_set_cq_callback(server->gcs_cq, gc_handle_server_event);
    server->gcs_server = grpc_server_create(NULL, NULL);
    server->gcs_host = strdup(host);
    gpr_mu_init(&server->gcs_lock);
    gpr_cv_init(&server->gcs_shutdown_cv);

    if (grpc_server_add_insecure_http2_port(server->gcs_server, host) == 0) {
	grpc_c_server_destroy(server);
	return NULL;
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
grpc_c_server_create (const char *name)
{
    char buf[BUFSIZ];

    if (name == NULL) {
	return NULL;
    }

    snprintf_safe(buf, sizeof(buf), "%s%s", PATH_GRPC_C_DAEMON_SOCK, name);

    if (buf[0] == '\0') {
	return NULL;
    }
    
    return gc_server_create_internal(buf);
}

/*
 * Adds insecure ip/port to grpc server
 */
int
grpc_c_server_add_insecure_http2_port (grpc_c_server_t *server, 
				       const char* addr UNUSED) 
{
    if (server == NULL) return 1;

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
    if (server == NULL) return 1;

    return grpc_server_add_secure_http2_port(server->gcs_server, addr, creds);
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

	if (server->gcs_host) free(server->gcs_host);

	if (server->gcs_server) {
	    grpc_server_shutdown_and_notify(server->gcs_server, 
					    server->gcs_cq, NULL);
	}

	if (server->gcs_cq) {
	    grpc_completion_queue_shutdown(server->gcs_cq);
	    while (grpc_completion_queue_next(server->gcs_cq, 
					      gpr_inf_past(GPR_CLOCK_REALTIME), 
					      NULL).type != GRPC_QUEUE_SHUTDOWN)
		;

	    if (server->gcs_server) {
		grpc_server_destroy(server->gcs_server);
	    }
	    grpc_completion_queue_destroy(server->gcs_cq);
	}

	/*
	 * Free pending contexts and method funcs
	 */
	if (server->gcs_method_funcs) {
	    for (i = 0; i < server->gcs_method_count; i++) {
		free(server->gcs_method_funcs[i].gcmf_name);
		grpc_completion_queue_shutdown(server->gcs_contexts[i]->gcc_cq);

		if (!grpc_c_get_thread_pool()) {
		    grpc_completion_queue_destroy(server->gcs_contexts[i]->gcc_cq);
		}

		grpc_c_context_free(server->gcs_contexts[i]);
	    }
	    free(server->gcs_method_funcs);
	    free(server->gcs_contexts);
	}

	while (!LIST_EMPTY(&server->gcs_method_list_head)) {
	    np = LIST_FIRST(&server->gcs_method_list_head);
	    LIST_REMOVE(np, gcm_list);

	    if (np->gcm_name) free(np->gcm_name);

	    free(np);
	}
	
	gpr_cv_broadcast(&server->gcs_callback_cv);
	gpr_cv_destroy(&server->gcs_shutdown_cv);
	gpr_mu_destroy(&server->gcs_lock);

	free(server);
    }
}
