/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <grpc-c/grpc-c.h>
#include <grpc/support/alloc.h>

#include "common/strextra.h"
#include "common/env_paths.h"

#include "context.h"
#include "thread_pool.h"
#include "hooks.h"
#include "stream_ops.h"

/*
 * Forward declaration
 */
static int gc_handle_client_event (grpc_completion_queue *cq);
static int gc_handle_connectivity_change (grpc_completion_queue *cq);

/*
 * Creates a channel to given host and returns instance of client by taking 
 * client-id and hostname
 */
static grpc_c_client_t * 
gc_client_create_by_host (const char *host, const char *id, 
			  grpc_channel_credentials *creds, 
			  grpc_channel_args *args)
{
    grpc_c_client_t *client;

    if (host == NULL || id == NULL) {
	gpr_log(GPR_ERROR, "Invalid hostname or client-id");
	return NULL;
    }

    client = gpr_malloc(sizeof(grpc_c_client_t));
    if (client == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory for client");
	return NULL;
    }
    
    bzero(client, sizeof(grpc_c_client_t));

    client->gcc_id = strdup(id);
    if (client->gcc_id == NULL) {
	gpr_log(GPR_ERROR, "Failed to copy client-id");
	grpc_c_client_free(client);
	return NULL;
    }

    client->gcc_host = grpc_slice_from_copied_string(host);

    /*
     * Create a channel using given hostname. If channel credentials are
     * provided, create a secure channel. Otherwise go for an insecure one
     */
    if (creds) {
	client->gcc_channel = grpc_secure_channel_create(creds, host, args, 
							 NULL);
    } else {
	client->gcc_channel = grpc_insecure_channel_create(host, args, NULL);
    }

    if (client->gcc_channel == NULL) {
	gpr_log(GPR_ERROR, "Failed to create a channel");
	grpc_c_client_free(client);
	return NULL;
    }

    /*
     * Initialize mutex and condition variables
     */
    gpr_mu_init(&client->gcc_lock);
    gpr_cv_init(&client->gcc_shutdown_cv);

    /*
     * Register server connect and disconnect callbacks
     */
    client->gcc_channel_connectivity_cq = grpc_completion_queue_create(NULL);
    if (client->gcc_channel_connectivity_cq == NULL) {
	gpr_log(GPR_ERROR, "Failed to create completion queue for server "
		"connect/disconnect notifications");
	grpc_c_client_free(client);
	return NULL;
    }

    if (grpc_c_get_type() > GRPC_THREADS) {
	grpc_c_grpc_set_cq_callback(client->gcc_channel_connectivity_cq, 
				    gc_handle_connectivity_change);
	client->gcc_channel_state 
	    = grpc_channel_check_connectivity_state(client->gcc_channel, 0);

	/*
	 * Watch for change in channel connectivity
	 */
	grpc_channel_watch_connectivity_state(client->gcc_channel, 
					      client->gcc_channel_state, 
					      gpr_inf_future(GPR_CLOCK_REALTIME), 
					      client->gcc_channel_connectivity_cq, 
					      (void *) client);
    }

    /*
     * Create list head to hold context objects that will have to be freed
     * before exit
     */
    LIST_INIT(&client->gcc_context_list_head);

    return client;
}

/*
 * Creates a client instance using hostname
 */
grpc_c_client_t *
grpc_c_client_init_by_host (const char *hostname, const char *client_id, 
			    grpc_channel_credentials *creds, 
			    grpc_channel_args *channel_args) 
{
    grpc_c_client_t *client;

    if (hostname == NULL || client_id == NULL) return NULL;

    client = gc_client_create_by_host(hostname, client_id, creds, 
				      channel_args);

    if (client != NULL) {
	if (grpc_c_get_thread_pool()) {
	    gpr_cv_init(&client->gcc_callback_cv);
	}
    }

    return client;
}

/*
 * Creates and initializes client by building socket path from server name
 */
grpc_c_client_t *
grpc_c_client_init (const char *server_name, const char *client_id, 
		    grpc_channel_credentials *creds, grpc_channel_args *args)
{
    grpc_c_client_t *client;
    char buf[BUFSIZ];

    assert(creds == NULL);

    if (server_name == NULL || client_id == NULL) {
	return NULL;
    }

    bzero(buf, BUFSIZ);
    snprintf_safe(buf, sizeof(buf), "%s%s", PATH_GRPC_C_DAEMON_SOCK, 
		  server_name);
    if (buf[0] == '\0') {
	return NULL;
    }

    return grpc_c_client_init_by_host(buf, client_id, creds, args);
}

/*
 * Free client instance and shutdown grpc
 */
void
grpc_c_client_free (grpc_c_client_t *client)
{
    grpc_c_context_t *ctx;

    if (client != NULL) {
	/*
	 * Wait till we are done with all the callbacks
	 */
	client->gcc_shutdown = 1;
	if (grpc_c_get_thread_pool()) {
	    gpr_mu_lock(&client->gcc_lock);
	    while (client->gcc_running_cb > 0 || client->gcc_wait) {
		gpr_cv_wait(&client->gcc_shutdown_cv, &client->gcc_lock, 
			    gpr_inf_future(GPR_CLOCK_REALTIME));
	    }
	    gpr_mu_unlock(&client->gcc_lock);
	}

	if (client->gcc_id) gpr_free(client->gcc_id);
	grpc_slice_unref(client->gcc_host);

	/*
	 * If we have any active context objects, free them before shutting
	 * down completion queue
	 */
	while (!LIST_EMPTY(&client->gcc_context_list_head)) {
	    ctx = LIST_FIRST(&client->gcc_context_list_head);
	    grpc_c_context_free(ctx);
	}

	if (client->gcc_channel) grpc_channel_destroy(client->gcc_channel);

	if (client->gcc_channel_connectivity_cq) {
	    grpc_completion_queue_shutdown(client->gcc_channel_connectivity_cq);
	    while (grpc_completion_queue_next(client->gcc_channel_connectivity_cq, 
					      gpr_inf_past(GPR_CLOCK_REALTIME), 
					      NULL).type != GRPC_QUEUE_SHUTDOWN)
		;

	    grpc_completion_queue_destroy(client->gcc_channel_connectivity_cq);
	}

	gpr_cv_signal(&client->gcc_callback_cv);
	gpr_cv_destroy(&client->gcc_shutdown_cv);
	gpr_mu_destroy(&client->gcc_lock);

	gpr_free(client);
    }
}

/*
 * Register connect callback from user
 */
void 
grpc_c_register_server_connect_callback (grpc_c_client_t *client, 
					 grpc_c_server_connect_callback_t *cb) 
{
    if (client != NULL) {
	client->gcc_server_connect_cb = cb;
    }
}

/*
 * Register disconnect callback from user
 */
void 
grpc_c_register_server_disconnect_callback (grpc_c_client_t *client, 
					    grpc_c_server_disconnect_callback_t *cb) 
{
    if (client != NULL) {
	client->gcc_server_disconnect_cb = cb;
    }
}

/*
 * Starts a grpc operation to get data from server. Returns 0 on success and 1
 * on failure
 */
static int
gc_client_request_data (grpc_c_context_t *context)
{
    int op_count = context->gcc_op_count;
    if (grpc_c_ops_alloc(context, 1)) return 1;

    context->gcc_ops[op_count].op = GRPC_OP_RECV_MESSAGE;
    context->gcc_ops[op_count].data.recv_message.recv_message 
	= &context->gcc_payload;
    context->gcc_op_count++;

    grpc_call_error e = grpc_call_start_batch(context->gcc_call, 
					      context->gcc_ops + op_count, 
					      1, context, NULL);
    if (e == GRPC_CALL_OK) {
	context->gcc_op_count--;
	return 0;
    } else {
	gpr_log(GPR_ERROR, "Failed to receive message from server: %d", e);
	return 1;
    }
}

/*
 * Starts a grpc operation to receive status from server. Returns 0 on success
 * of operation and 1 on failure. Status of RPC execution is saved in
 * context->gcc_status. This will be called once the server indicates end of
 * output by sending a NULL.
 */
static int
gc_client_request_status (grpc_c_context_t *context)
{    
    int op_count = context->gcc_op_count;

    if (grpc_c_ops_alloc(context, 1)) return 1;

    context->gcc_ops[op_count].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    context->gcc_ops[op_count].data.recv_status_on_client.trailing_metadata 
	= context->gcc_trailing_metadata;
    context->gcc_ops[op_count].data.recv_status_on_client.status 
	= &context->gcc_status;
    context->gcc_ops[op_count].data.recv_status_on_client.status_details 
	= &context->gcc_status_details;
    context->gcc_op_count++;

    grpc_call_error e = grpc_call_start_batch(context->gcc_call, 
					      context->gcc_ops + op_count, 
					      1, context, NULL);
    if (e == GRPC_CALL_OK) {
	context->gcc_op_count--;
	context->gcc_state = GRPC_C_CLIENT_DONE;
	return 0;
    } else {
	gpr_log(GPR_ERROR, "Failed to get status from server: %d", e);
	return 1;
    }
}

/*
 * On receiving a successful complete op on connectivity cq, this handles
 * connect and disconnect callbacks
 */
static int 
gc_handle_client_connectivity_complete_op (grpc_c_client_t *client, 
					   grpc_completion_queue *cq)
{
    int shutdown = 0;

    if (client == NULL || cq == NULL) {
	gpr_log(GPR_ERROR, "Invalid client or cq in connectivity completion\n");
	return shutdown;
    }

    grpc_connectivity_state s 
	= grpc_channel_check_connectivity_state(client->gcc_channel, 0);

    /*
     * If our current state is failure, our connection to server is dropped
     */
    if (s == GRPC_CHANNEL_TRANSIENT_FAILURE || s == GRPC_CHANNEL_SHUTDOWN 
	|| (client->gcc_channel_state == GRPC_CHANNEL_READY 
	    && s == GRPC_CHANNEL_IDLE)) {
	/*
	 * If we are already connected and a disconnect cb is registered, call
	 * it
	 */
	if (client->gcc_server_disconnect_cb && client->gcc_connected) {
	    client->gcc_server_disconnect_cb(client);
	}
	client->gcc_connected = 0;
	shutdown = 1;
    } else if (s == GRPC_CHANNEL_READY) {
	/*
	 * If this is from a retry attempt, we have to stop retrying now
	 */
	if (client->gcc_retry_tag && client->gcc_connected == 0) {
	    grpc_c_grpc_client_cancel_try_connect(client->gcc_retry_tag);
	    client->gcc_retry_tag = NULL;
	}

	/*
	 * If our previous state is connecting and we are ready now, we just
	 * established a connection
	 */
	if (client->gcc_server_connect_cb && client->gcc_connected == 0) {
	    client->gcc_connected = 1;
	    client->gcc_server_connect_cb(client);
	}
    }
    client->gcc_channel_state = s;

    /*
     * Watch for change in channel connectivity
     */
    grpc_channel_watch_connectivity_state(client->gcc_channel, s, 
					  gpr_inf_future(GPR_CLOCK_REALTIME), 
					  cq, client);
    return shutdown;
}

/*
 * Handler for connection state change events. This gets called whenever there
 * is a change in the state of connection in a given channel. Event tag has a
 * pointer to client object
 */
static int
gc_handle_connectivity_change (grpc_completion_queue *cq)
{
    grpc_event ev;
    grpc_c_client_t *client = NULL;
    int shutdown = 0, timeout = 0, rc = 0;

    while (!shutdown && !timeout) {
	timeout = 0;
	ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME), 
					NULL);
	switch (ev.type) {
	    case GRPC_OP_COMPLETE:
		/*
		 * If the op failed, skip handling it
		 */
		if (ev.success == 0) break;

		/*
		 * Get current channel connectivity state and compare it
		 * against the last state
		 */
		client = (grpc_c_client_t *)ev.tag;
		if (gc_handle_client_connectivity_complete_op(client, cq)) {
		    shutdown = 1;
		}
		break;
	    case GRPC_QUEUE_SHUTDOWN:
		shutdown = 1;
		break;
	    case GRPC_QUEUE_TIMEOUT:
		timeout = 1;
		break;
	    default:
		gpr_log(GPR_INFO, "Unknown event type");
		rc = -1;
		timeout = 1;
	}
    }

    return rc;
}

/*
 * Internal function to handle events in a completion queue
 */
static int 
gc_handle_client_event_internal (grpc_completion_queue *cq, 
				 gpr_timespec ts)
{
    grpc_c_context_t *context;
    grpc_c_event_t *gcev;
    grpc_c_client_t *client = NULL;
    gpr_mu *context_lock = NULL;
    grpc_event ev;
    int shutdown = 0, timeout = 0, rc = 0;

    while (!shutdown && !timeout) {
	timeout = 0;
	ev = grpc_completion_queue_next(cq, ts, NULL);

	switch (ev.type) {
	    case GRPC_OP_COMPLETE:
		gcev = (grpc_c_event_t *)ev.tag;
		context = (grpc_c_context_t *)(gcev->gce_data);
		if (context) {
		    client = context->gcc_data.gccd_client;
		}

		/*
		 * If the event succesfully completed, let the client handle
		 * the response depending on batch type. Else silently clean 
		 * the context
		 */
		if (gcev->gce_type == GRPC_C_EVENT_RPC_INIT) {
		    grpc_c_stream_handler_t *stream_handler 
			= context->gcc_stream;
		    stream_handler->read = &gc_stream_read;
		    stream_handler->write = &gc_stream_write;
		    stream_handler->write_done = &gc_client_stream_write_done;
		    stream_handler->finish = &gc_client_stream_finish;

		    /*
		     * Create a context lock to syncronize access to this cq
		     */
		    context->gcc_lock = gpr_malloc(sizeof(gpr_mu));
		    if (context->gcc_lock == NULL) {
			gpr_log(GPR_ERROR, "Failed to allocate context lock");
			break;
		    }
		    gpr_mu_init(context->gcc_lock);
		    context_lock = context->gcc_lock;

		    context->gcc_method_funcs->gcmf_handler
			.gcmfh_client(context, context->gcc_tag, ev.success);
		} else if (gcev->gce_type == GRPC_C_EVENT_READ) {
		    /*
		     * Our read has resolved. If the user has set a callback, 
		     * invoke it
		     */
		    if (context->gcc_read_resolve_cb) {
			(context->gcc_read_resolve_cb)
			    (context, context->gcc_read_resolve_arg, 
			     ev.success);
			context->gcc_read_resolve_cb = NULL;
		    }
		} else if (gcev->gce_type == GRPC_C_EVENT_WRITE) {
		    /*
		     * Our pending write has resolved. We can invoke user 
		     * provided callback so he can continue writing
		     */
		    if (context->gcc_write_resolve_cb) {
			(context->gcc_write_resolve_cb)
			    (context, context->gcc_write_resolve_arg, 
			     ev.success);
			context->gcc_write_resolve_cb = NULL;
		    }
		} else if (gcev->gce_type == GRPC_C_EVENT_WRITE_FINISH) {
		}
		gpr_mu_lock(context->gcc_lock);
		gcev->gce_refcount--;
		gpr_mu_unlock(context->gcc_lock);
		break;
	    case GRPC_QUEUE_SHUTDOWN:
		grpc_completion_queue_destroy(cq);
		if (client) {
		    gpr_mu_lock(&client->gcc_lock);
		    client->gcc_running_cb--;
		    gpr_mu_unlock(&client->gcc_lock);
		    /*
		     * If we are done executing all the callbacks, finish 
		     * shutdown
		     */
		    if (grpc_c_get_thread_pool()) {
			if (client->gcc_running_cb == 0 
			    && client->gcc_shutdown) {
			    gpr_cv_signal(&client->gcc_shutdown_cv);
			}
			gpr_cv_signal(&client->gcc_callback_cv);
		    }
		}
		shutdown = 1;
		break;
	    case GRPC_QUEUE_TIMEOUT:
		timeout = 1;
		break;
	    default:
		gpr_log(GPR_INFO, "Unknown event type");
		rc = -1;
		timeout = 1;
	}
    }
    return rc;
}

/*
 * Waits out for callback from RPC execution
 */
static void 
gc_run_rpc (void *arg)
{
    grpc_completion_queue *cq = (grpc_completion_queue *)arg;

    gc_handle_client_event_internal(cq, gpr_inf_future(GPR_CLOCK_REALTIME));
}

/*
 * Handler for completion_queue event. This gets called whenever a batch
 * operation on a completion queue is finished
 */
static int 
gc_handle_client_event (grpc_completion_queue *cq)
{
    return gc_handle_client_event_internal(cq, gpr_inf_past(GPR_CLOCK_REALTIME));
}

/*
 * Returns a context object with filled in ops for async call
 */
static grpc_c_context_t *
gc_client_prepare_async_ops (grpc_c_client_t *client, 
			     grpc_c_metadata_array_t *mdarray, uint32_t flags, 
			     void *input, grpc_c_client_callback_t *cb, 
			     void *tag, int client_streaming, 
			     int server_streaming, 
			     grpc_c_method_data_pack_t *input_packer, 
			     grpc_c_method_data_unpack_t *input_unpacker, 
			     grpc_c_method_data_free_t *input_free, 
			     grpc_c_method_data_pack_t *output_packer, 
			     grpc_c_method_data_unpack_t *output_unpacker, 
			     grpc_c_method_data_free_t *output_free)
{
    int mdcount = 0;
    char *mdkey, *mdvalue;
    grpc_c_context_t *context = grpc_c_context_init(NULL, 1);
    if (context == NULL) {
	gpr_log(GPR_ERROR, "Failed to create context");
	return NULL;
    }

    context->gcc_method = gpr_malloc(sizeof(struct grpc_c_method_t));
    if (context->gcc_method == NULL) {
	grpc_c_context_free(context);
	return NULL;
    }
    bzero(context->gcc_method, sizeof(struct grpc_c_method_t));

    context->gcc_state = GRPC_C_CLIENT_START;
    context->gcc_cq = grpc_completion_queue_create(NULL);
    grpc_c_grpc_set_cq_callback(context->gcc_cq, gc_handle_client_event);

    int op_count = context->gcc_op_count;

    /*
     * Save packer, unpacker, free functions for input/output and client
     * callback, client provided tag
     */
    context->gcc_method_funcs->gcmf_handler.gcmfh_client = cb;
    context->gcc_method->gcm_client_streaming = client_streaming;
    context->gcc_method->gcm_server_streaming = server_streaming;
    context->gcc_method_funcs->gcmf_input_packer = input_packer;
    context->gcc_method_funcs->gcmf_input_unpacker = input_unpacker;
    context->gcc_method_funcs->gcmf_input_free = input_free;
    context->gcc_method_funcs->gcmf_output_packer = output_packer;
    context->gcc_method_funcs->gcmf_output_unpacker = output_unpacker;
    context->gcc_method_funcs->gcmf_output_free = output_free;
    context->gcc_data.gccd_client = client;
    context->gcc_tag = tag;

    /*
     * We send the message if input is provided.
     */
    if (grpc_c_ops_alloc(context, 2 + (input ? 1 : 0))) {
	grpc_c_context_free(context);
	return NULL;
    }

    /*
     * We need to send client-id as part of metadata with each RPC call. Make
     * space to hold metadata
     */
    if (grpc_c_add_metadata(context, "client-id", 
			    client->gcc_id ? client->gcc_id : "")) {
	grpc_c_context_free(context);
	gpr_log(GPR_ERROR, "Failed to add client-id to metadata");
	return NULL;
    }
    
    /*
     * Stuff given metadata into the call
     */
    if (mdarray != NULL) {
	for (mdcount = 0; mdcount < mdarray->count; mdcount++) {
	    mdkey = grpc_slice_to_c_string(mdarray->metadata[mdcount].key);
	    mdvalue = grpc_slice_to_c_string(mdarray->metadata[mdcount].value);
	    if (grpc_c_add_metadata(context, (const char *)mdkey, 
				    (const char *)mdvalue)) {
		gpr_log(GPR_ERROR, "Failed to add metadata");
		return NULL;
	    }
	    gpr_free(mdkey);
	    gpr_free(mdvalue);
	}
    }

    /*
     * Send initial metadata with client-id
     */
    context->gcc_ops[op_count].op = GRPC_OP_SEND_INITIAL_METADATA;
    context->gcc_ops[op_count].flags = flags;
    context->gcc_ops[op_count].data.send_initial_metadata.count = mdcount + 1;
    context->gcc_ops[op_count].data.send_initial_metadata.metadata 
	= &context->gcc_metadata->metadata[context->gcc_metadata->count - 1];
    op_count++;

    /*
     * Encode data to be sent into wire format
     */
    if (input) {
	input_packer(input, &context->gcc_ops_payload[op_count]);

	context->gcc_ops[op_count].op = GRPC_OP_SEND_MESSAGE;
	context->gcc_ops[op_count].data.send_message.send_message 
	    = context->gcc_ops_payload[op_count];
	op_count++;
    }

    context->gcc_ops[op_count].op = GRPC_OP_RECV_INITIAL_METADATA;
    context->gcc_ops[op_count].data.recv_initial_metadata.recv_initial_metadata 
	= context->gcc_initial_metadata;
    context->gcc_meta_sent = 1;
    op_count++;
    context->gcc_op_count = op_count;

    /*
     * Add this context object to list head so we can track this
     */
    LIST_INSERT_HEAD(&context->gcc_data.gccd_client->gcc_context_list_head, 
		     context, gcc_list);

    /*
     * If we are async, we need a thread in background to process events
     * from completion queue. Otherwise we pluck in the same thread
     */
    if (grpc_c_get_thread_pool() && !client->gcc_shutdown) {
	grpc_c_thread_pool_add(grpc_c_get_thread_pool(), gc_run_rpc, 
			       context->gcc_cq);
    }
    gpr_mu_lock(&client->gcc_lock);
    client->gcc_running_cb++;
    gpr_mu_unlock(&client->gcc_lock);

    return context;
}


/*
 * Returns a context object with filled in ops for sync call
 */
static grpc_c_context_t *
gc_client_prepare_sync_ops (grpc_c_client_t *client, 
			    grpc_c_metadata_array_t *mdarray, uint32_t flags, 
			    void *input, void *tag, int client_streaming, 
			    int server_streaming, 
			    grpc_c_client_callback_t *cb, 
			    grpc_c_method_data_pack_t *input_packer, 
			    grpc_c_method_data_unpack_t *input_unpacker, 
			    grpc_c_method_data_free_t *input_free, 
			    grpc_c_method_data_pack_t *output_packer, 
			    grpc_c_method_data_unpack_t *output_unpacker, 
			    grpc_c_method_data_free_t *output_free)
{
    int mdcount = 0;
    char *mdkey, *mdvalue;
    grpc_c_context_t *context = grpc_c_context_init(NULL, 1);
    if (context == NULL) {
	gpr_log(GPR_ERROR, "Failed to create context");
	return NULL;
    }

    context->gcc_method = gpr_malloc(sizeof(struct grpc_c_method_t));
    if (context->gcc_method == NULL) {
	grpc_c_context_free(context);
	return NULL;
    }
    bzero(context->gcc_method, sizeof(struct grpc_c_method_t));

    grpc_c_stream_handler_t *stream_handler;
    if (context->gcc_stream == NULL) {
	stream_handler = gpr_malloc(sizeof(grpc_c_stream_handler_t));
    } else {
	stream_handler = context->gcc_stream;
    }

    if (stream_handler == NULL) {
	grpc_c_context_free(context);
	return NULL;
    }

    stream_handler->read = &gc_stream_read;
    stream_handler->write = &gc_stream_write;
    stream_handler->write_done = &gc_client_stream_write_done;
    stream_handler->finish = &gc_client_stream_finish;
    context->gcc_stream = stream_handler;

    context->gcc_lock = gpr_malloc(sizeof(gpr_mu));
    if (context->gcc_lock == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate context lock");
	grpc_c_context_free(context);
	return NULL;
    }
    gpr_mu_init(context->gcc_lock);

    context->gcc_state = GRPC_C_CLIENT_START;
    context->gcc_cq = grpc_completion_queue_create(NULL);
    grpc_c_grpc_set_cq_callback(context->gcc_cq, gc_handle_client_event);

    int op_count = context->gcc_op_count;

    /*
     * Save packer, unpacker, free functions for input/output and client
     * callback, client provided tag
     */
    context->gcc_method_funcs->gcmf_handler.gcmfh_client = cb;
    context->gcc_method->gcm_client_streaming = client_streaming;
    context->gcc_method->gcm_server_streaming = server_streaming;
    context->gcc_method_funcs->gcmf_input_packer = input_packer;
    context->gcc_method_funcs->gcmf_input_unpacker = input_unpacker;
    context->gcc_method_funcs->gcmf_input_free = input_free;
    context->gcc_method_funcs->gcmf_output_packer = output_packer;
    context->gcc_method_funcs->gcmf_output_unpacker = output_unpacker;
    context->gcc_method_funcs->gcmf_output_free = output_free;
    context->gcc_data.gccd_client = client;
    context->gcc_tag = tag;

    /*
     * Allocate ops
     */
    if (grpc_c_ops_alloc(context, 2 + (input ? 1 : 0))) {
	grpc_c_context_free(context);
	return NULL;
    }

    /*
     * We need to send client-id as part of metadata with each RPC call. Make
     * space to hold metadata
     */
    if (grpc_c_add_metadata(context, "client-id", 
			    client->gcc_id ? client->gcc_id : "")) {
	grpc_c_context_free(context);
	gpr_log(GPR_ERROR, "Failed to add client-id to metadata");
	return NULL;
    }
    
    /*
     * Stuff given metadata into the call
     */
    if (mdarray != NULL) {
	for (mdcount = 0; mdcount < mdarray->count; mdcount++) {
	    mdkey = grpc_slice_to_c_string(mdarray->metadata[mdcount].key);
	    mdvalue = grpc_slice_to_c_string(mdarray->metadata[mdcount].value);
	    if (grpc_c_add_metadata(context, (const char *)mdkey, 
				    (const char *)mdvalue)) {
		gpr_log(GPR_ERROR, "Failed to add metadata");
		return NULL;
	    }
	    gpr_free(mdkey);
	    gpr_free(mdvalue);
	}
    }

    /*
     * Send initial metadata with client-id
     */
    context->gcc_ops[op_count].op = GRPC_OP_SEND_INITIAL_METADATA;
    context->gcc_ops[op_count].flags = flags;
    context->gcc_ops[op_count].data.send_initial_metadata.count = mdcount + 1;
    context->gcc_ops[op_count].data.send_initial_metadata.metadata 
	= &context->gcc_metadata->metadata[context->gcc_metadata->count - 1];
    op_count++;

    /*
     * Encode data to be sent into wire format
     */
    if (input) {
	input_packer(input, &context->gcc_ops_payload[op_count]);

	context->gcc_ops[op_count].op = GRPC_OP_SEND_MESSAGE;
	context->gcc_ops[op_count].data.send_message.send_message 
	    = context->gcc_ops_payload[op_count];
	op_count++;
    }

    context->gcc_ops[op_count].op = GRPC_OP_RECV_INITIAL_METADATA;
    context->gcc_ops[op_count].data.recv_initial_metadata.recv_initial_metadata  
	= context->gcc_initial_metadata;
    context->gcc_meta_sent = 1;
    op_count++;
    context->gcc_op_count = op_count;

    /*
     * Add this context object to list head so we can track this
     */
    LIST_INSERT_HEAD(&context->gcc_data.gccd_client->gcc_context_list_head, 
		     context, gcc_list);
    gpr_mu_lock(&client->gcc_lock);
    client->gcc_running_cb++;
    gpr_mu_unlock(&client->gcc_lock);

    return context;
}

/*
 * Creates a context and fills initial operations for calling RPC and sending
 * initial metadata, data and request for data from server
 */
static grpc_c_context_t *
gc_client_prepare_unary_ops (grpc_c_client_t *client, 
			     grpc_c_metadata_array_t *mdarray, uint32_t flags, 
			     void *input, void *tag, int client_streaming, 
			     int server_streaming, 
			     grpc_c_client_callback_t *cb, 
			     grpc_c_method_data_pack_t *input_packer, 
			     grpc_c_method_data_unpack_t *input_unpacker, 
			     grpc_c_method_data_free_t *input_free, 
			     grpc_c_method_data_pack_t *output_packer, 
			     grpc_c_method_data_unpack_t *output_unpacker, 
			     grpc_c_method_data_free_t *output_free)
{
    int mdcount = 0;
    char *mdkey, *mdvalue;
    grpc_c_context_t *context = grpc_c_context_init(NULL, 1);
    if (context == NULL) {
	gpr_log(GPR_ERROR, "Failed to create context");
	return NULL;
    }

    context->gcc_method = gpr_malloc(sizeof(struct grpc_c_method_t));
    if (context->gcc_method == NULL) {
	grpc_c_context_free(context);
	return NULL;
    }
    bzero(context->gcc_method, sizeof(struct grpc_c_method_t));

    context->gcc_state = GRPC_C_CLIENT_START;
    context->gcc_cq = grpc_completion_queue_create(NULL);
    grpc_c_grpc_set_cq_callback(context->gcc_cq, gc_handle_client_event);

    int op_count = context->gcc_op_count;

    /*
     * Save packer, unpacker, free functions for input/output and client
     * callback, client provided tag
     */
    context->gcc_method_funcs->gcmf_handler.gcmfh_client = cb;
    context->gcc_method->gcm_client_streaming = client_streaming;
    context->gcc_method->gcm_server_streaming = server_streaming;
    context->gcc_method_funcs->gcmf_input_packer = input_packer;
    context->gcc_method_funcs->gcmf_input_unpacker = input_unpacker;
    context->gcc_method_funcs->gcmf_input_free = input_free;
    context->gcc_method_funcs->gcmf_output_packer = output_packer;
    context->gcc_method_funcs->gcmf_output_unpacker = output_unpacker;
    context->gcc_method_funcs->gcmf_output_free = output_free;
    context->gcc_data.gccd_client = client;
    context->gcc_tag = tag;

    /*
     * We do not close and request for status on server streaming. Instead we 
     * continue issuing GRPC_OP_RECV_MESSAGE in other functions to continue 
     * receiving stream of data from server. We will need 5 ops when streaming 
     * and 6 for non-streaming server.
     */
    if (grpc_c_ops_alloc(context, 5 + (server_streaming ? 0 : 1))) {
	grpc_c_context_free(context);
	return NULL;
    }

    /*
     * We need to send client-id as part of metadata with each RPC call. Make
     * space to hold metadata
     */
    if (grpc_c_add_metadata(context, "client-id", 
			    client->gcc_id ? client->gcc_id : "")) {
	grpc_c_context_free(context);
	gpr_log(GPR_ERROR, "Failed to add client-id to metadata");
	return NULL;
    }
    
    /*
     * Stuff given metadata into the call
     */
    if (mdarray != NULL) {
	for (mdcount = 0; mdcount < mdarray->count; mdcount++) {
	    mdkey = grpc_slice_to_c_string(mdarray->metadata[mdcount].key);
	    mdvalue = grpc_slice_to_c_string(mdarray->metadata[mdcount].value);
	    if (grpc_c_add_metadata(context, (const char *)mdkey, 
				    (const char *)mdvalue)) {
		gpr_log(GPR_ERROR, "Failed to add metadata");
		return NULL;
	    }
	    gpr_free(mdkey);
	    gpr_free(mdvalue);
	}
    }

    /*
     * Send initial metadata with client-id
     */
    context->gcc_ops[op_count].op = GRPC_OP_SEND_INITIAL_METADATA;
    context->gcc_ops[op_count].flags = flags;
    context->gcc_ops[op_count].data.send_initial_metadata.count = mdcount + 1;
    context->gcc_ops[op_count].data.send_initial_metadata.metadata 
	= &context->gcc_metadata->metadata[context->gcc_metadata->count - 1];
    op_count++;

    /*
     * Encode data to be sent into wire format
     */
    input_packer(input, &context->gcc_ops_payload[op_count]);

    context->gcc_ops[op_count].op = GRPC_OP_SEND_MESSAGE;
    context->gcc_ops[op_count].data.send_message.send_message  
	= context->gcc_ops_payload[op_count];
    op_count++;

    context->gcc_ops[op_count].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op_count++;

    context->gcc_ops[op_count].op = GRPC_OP_RECV_INITIAL_METADATA;
    context->gcc_ops[op_count].data.recv_initial_metadata.recv_initial_metadata 
	= context->gcc_initial_metadata;
    op_count++;

    context->gcc_ops[op_count].op = GRPC_OP_RECV_MESSAGE;
    context->gcc_ops[op_count].data.recv_message.recv_message 
	= &context->gcc_payload;
    op_count++;

    /*
     * Request status on client if we have a non-streaming server
     */
    if (server_streaming == 0) {
	context->gcc_ops[op_count].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
	context->gcc_ops[op_count].data.recv_status_on_client.trailing_metadata 
	    = context->gcc_trailing_metadata;
	context->gcc_ops[op_count].data.recv_status_on_client.status 
	    = &context->gcc_status;
	context->gcc_ops[op_count].data.recv_status_on_client.status_details 
	    = &context->gcc_status_details;
	op_count++;

	/*
	 * Set meta_sent flag so we know that we have already requested for
	 * status
	 */
	context->gcc_meta_sent = 1;
	context->gcc_state = GRPC_C_CLIENT_DONE;
    }

    context->gcc_op_count = op_count;

    /*
     * Add this context object to list head so we can track this
     */
    LIST_INSERT_HEAD(&context->gcc_data.gccd_client->gcc_context_list_head, 
		     context, gcc_list);

    return context;
}

/*
 * Main function that handles streaming/async RPC calls. Returns 1 on error
 * and 0 on success. This return value is propagated to the client by the
 * autogenerated functions. Client is expected to check the return status of
 * these functions.
 */
int
grpc_c_client_request_async (grpc_c_client_t *client, 
			     grpc_c_metadata_array_t *mdarray, uint32_t flags, 
			     const char *method, void *input, 
			     grpc_c_client_callback_t *cb, void *tag, 
			     int client_streaming, int server_streaming, 
			     grpc_c_method_data_pack_t *input_packer, 
			     grpc_c_method_data_unpack_t *input_unpacker, 
			     grpc_c_method_data_free_t *input_free, 
			     grpc_c_method_data_pack_t *output_packer, 
			     grpc_c_method_data_unpack_t *output_unpacker, 
			     grpc_c_method_data_free_t *output_free)
{
    grpc_call_error e;
    grpc_c_context_t *context = gc_client_prepare_async_ops(client, mdarray, 
							    flags, input, cb, 
							    tag, 
							    client_streaming, 
							    server_streaming, 
							    input_packer, 
							    input_unpacker, 
							    input_free, 
							    output_packer, 
							    output_unpacker, 
							    output_free);
    if (context == NULL) {
	gpr_log(GPR_ERROR, "Failed to create context with async operations");
	return GRPC_C_FAIL;
    }

    /*
     * Create call to the required RPC
     */
    context->gcc_call 
	= grpc_channel_create_call(client->gcc_channel, NULL, 0, 
				   context->gcc_cq, 
				   grpc_slice_from_static_string(method), 
				   &client->gcc_host, 
				   gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    context->gcc_event.gce_data = context;

    e = grpc_call_start_batch(context->gcc_call, context->gcc_ops, 
			      context->gcc_op_count, &context->gcc_event, NULL);

    if (e == GRPC_CALL_OK) {
	context->gcc_op_count = 0;
	return GRPC_C_OK;
    } else {
	gpr_log(GPR_ERROR, "Failed to finish batch operations on async call: "
		"%d", e);
	grpc_c_context_free(context);
	return GRPC_C_FAIL;
    }
}

/*
 * Unary function
 */
int
grpc_c_client_request_unary (grpc_c_client_t *client, 
			     grpc_c_metadata_array_t *mdarray, uint32_t flags, 
			     const char *method, void *input, void **output, 
			     grpc_c_status_t *status, int client_streaming, 
			     int server_streaming, 
			     grpc_c_method_data_pack_t *input_packer, 
			     grpc_c_method_data_unpack_t *input_unpacker, 
			     grpc_c_method_data_free_t *input_free, 
			     grpc_c_method_data_pack_t *output_packer, 
			     grpc_c_method_data_unpack_t *output_unpacker, 
			     grpc_c_method_data_free_t *output_free, 
			     long timeout)
{
    int rc = GRPC_C_OK;
    grpc_call_error e;
    grpc_event ev;
    gpr_timespec tout;
    grpc_c_context_t *context = gc_client_prepare_unary_ops(client, mdarray,  
							    flags, input, NULL, 
							    client_streaming, 
							    server_streaming, 
							    NULL, input_packer, 
							    input_unpacker, 
							    input_free, 
							    output_packer, 
							    output_unpacker, 
							    output_free); 
    if (context == NULL) {
	gpr_log(GPR_ERROR, "Failed to create context with sync operations");
	rc = GRPC_C_FAIL;
	goto cleanup;
    }
    
    context->gcc_call 
	= grpc_channel_create_call(client->gcc_channel, NULL, 0, 
				   context->gcc_cq, 
				   grpc_slice_from_static_string(method), 
				   &client->gcc_host, 
				   gpr_inf_future(GPR_CLOCK_REALTIME), NULL);

    e = grpc_call_start_batch(context->gcc_call, context->gcc_ops, 
			      context->gcc_op_count, context, NULL);

    if (e == GRPC_CALL_OK) {
	context->gcc_op_count = 0;
    } else {
	grpc_c_context_free(context);
	gpr_log(GPR_ERROR, "Failed to finish batch operations on sync call: "
		"%d", e);
	rc = GRPC_C_FAIL;
	goto cleanup;
    }

    /*
     * In sync operations, we block till the event we are interested in is
     * available. If timeout is -1, then we block till call is executed
     */
    tout = gc_deadline_from_timeout(timeout);

    ev = grpc_completion_queue_pluck(context->gcc_cq, context, tout, NULL);

    /*
     * If our sync call has timedout, cancel the call and return timedout. If
     * cancel fails, mark that as failed call
     */
    if (ev.type == GRPC_QUEUE_TIMEOUT 
	&& grpc_call_cancel(context->gcc_call, NULL) == GRPC_CALL_OK) {
	gpr_log(GPR_ERROR, "Sync call timedout");
	rc = GRPC_C_TIMEOUT;
	goto cleanup;
    } else if (ev.type != GRPC_OP_COMPLETE || ev.success == 0) {
	grpc_c_context_free(context);
	gpr_log(GPR_ERROR, "Failed to pluck sync event");
	rc = GRPC_C_FAIL;
	goto cleanup;
    }

    /*
     * Decode the received data and point client provided pointer to this data
     */
    if (context->gcc_payload) {
	*output = output_unpacker(context, context->gcc_payload);
    } else {
	*output = NULL;
    }

    if (*output == NULL) {
	grpc_c_context_free(context);
	gpr_log(GPR_ERROR, "No output to return");
	rc = GRPC_C_FAIL;
	goto cleanup;
    }

    /*
     * If we are given a pointer to status, fill it with return code from
     * server and message if any
     */
    if (status) {
	status->gcs_code = context->gcc_status;
	char *status_message 
	    = grpc_slice_to_c_string(context->gcc_status_details);
	if (status_message) {
	    strlcpy(status->gcs_message, status_message, 
		    sizeof(status->gcs_message));
	} else {
	    status->gcs_message[0] = '\0';
	}
    }

    grpc_completion_queue *cq = context->gcc_cq;
    grpc_c_context_free(context);
    /*
     * We can destroy the completion_queue once it is shutdown
     */
    while (grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME), 
				      NULL).type != GRPC_QUEUE_SHUTDOWN)
	;
    grpc_completion_queue_destroy(cq);

cleanup:
    gpr_mu_lock(&client->gcc_lock);
    client->gcc_running_cb--;
    gpr_mu_unlock(&client->gcc_lock);
    /*
     * If we are done executing all the callbacks, finish shutdown
     */
    if (grpc_c_get_thread_pool()) {
	if (client->gcc_running_cb == 0 && client->gcc_shutdown) {
	    gpr_cv_signal(&client->gcc_shutdown_cv);
	}

        gpr_cv_signal(&client->gcc_callback_cv);
    }

    return rc;
}

/*
 * Main function that handles sync/non-streaming RPC calls. Returns 1 on error
 * and 0 on success. Client is expected to check the return status of the
 * autogenerated functions which propagate return value of this function
 */
int
grpc_c_client_request_sync (grpc_c_client_t *client, 
			    grpc_c_metadata_array_t *mdarray, uint32_t flags,  
			    grpc_c_context_t **pcontext, const char *method, 
			    void *input, int client_streaming, 
			    int server_streaming, 
			    grpc_c_method_data_pack_t *input_packer, 
			    grpc_c_method_data_unpack_t *input_unpacker, 
			    grpc_c_method_data_free_t *input_free, 
			    grpc_c_method_data_pack_t *output_packer, 
			    grpc_c_method_data_unpack_t *output_unpacker, 
			    grpc_c_method_data_free_t *output_free, 
			    long timeout)
{
    int rc = GRPC_C_OK;
    grpc_call_error e;
    grpc_event ev;
    gpr_timespec tout;
    grpc_c_context_t *context;

    if (pcontext == NULL) {
	gpr_log(GPR_ERROR, "Invalid context pointer provided");
	rc = GRPC_C_FAIL;
	goto cleanup;
    }

    context = gc_client_prepare_sync_ops(client, mdarray, flags, input, NULL, 
					 client_streaming, server_streaming, 
					 NULL, input_packer, input_unpacker, 
					 input_free, output_packer, 
					 output_unpacker, output_free); 
    if (context == NULL) {
	gpr_log(GPR_ERROR, "Failed to create context with sync operations");
	rc = GRPC_C_FAIL;
	goto cleanup;
    }

    *pcontext = context;

    context->gcc_call 
	= grpc_channel_create_call(client->gcc_channel, NULL, 0, 
				   context->gcc_cq, 
				   grpc_slice_from_static_string(method), 
				   &client->gcc_host, 
				   gpr_inf_future(GPR_CLOCK_REALTIME), NULL);

    e = grpc_call_start_batch(context->gcc_call, context->gcc_ops, 
			      context->gcc_op_count, context, NULL);

    if (e == GRPC_CALL_OK) {
	context->gcc_op_count = 0;
    } else {
	grpc_c_context_free(context);
	gpr_log(GPR_ERROR, "Failed to finish batch operations on sync call: "
		"%d", e);
	rc = GRPC_C_FAIL;
	goto cleanup;
    }

    /*
     * In sync operations, we block till the event we are interested in is
     * available. If timeout is -1, then we block till call is executed
     */
    tout = gc_deadline_from_timeout(timeout);

    ev = grpc_completion_queue_pluck(context->gcc_cq, context, tout, NULL);

    /*
     * If our sync call has timedout, cancel the call and return timedout. If
     * cancel fails, mark that as failed call
     */
    if (ev.type == GRPC_QUEUE_TIMEOUT 
	&& grpc_call_cancel(context->gcc_call, NULL) == GRPC_CALL_OK) {
	gpr_log(GPR_ERROR, "Sync call timedout");
	rc = GRPC_C_TIMEOUT;
	goto cleanup;
    } else if (ev.type != GRPC_OP_COMPLETE || ev.success == 0) {
	grpc_c_context_free(context);
	gpr_log(GPR_ERROR, "Failed to pluck sync event");
	rc = GRPC_C_FAIL;
	goto cleanup;
    }

cleanup:
    if (grpc_c_get_thread_pool()) {
	if (client->gcc_running_cb == 0 && client->gcc_shutdown) {
	    gpr_cv_signal(&client->gcc_shutdown_cv);
	}

        gpr_cv_signal(&client->gcc_callback_cv);
    }

    return rc;
}

/*
 * Waits for all the callbacks to finish execution
 */
void 
grpc_c_client_wait (grpc_c_client_t *client) 
{
    gpr_mu mu;
    client->gcc_wait = 1;
    gpr_mu_init(&mu);
    gpr_cv_init(&client->gcc_callback_cv);
    gpr_mu_lock(&mu);
    while (client->gcc_running_cb > 0) {
	gpr_cv_wait(&client->gcc_callback_cv, &mu, 
		    gpr_inf_future(GPR_CLOCK_REALTIME));
    }
    gpr_mu_unlock(&mu);
    gpr_mu_destroy(&mu);
    gpr_cv_destroy(&client->gcc_callback_cv);

    client->gcc_wait = 0;
    gpr_cv_signal(&client->gcc_shutdown_cv);
}

/*
 * Sets client task pointer
 */
void
grpc_c_set_client_task (void *tp)
{
    grpc_c_grpc_set_client_task(tp);
}

static void 
gc_client_retry_timeout_cb (void *data) 
{
    grpc_c_client_t *client = (grpc_c_client_t *)data;
    client->gcc_conn_timeout = 1;
    client->gcc_connected = 0;
    client->gcc_retry_tag = NULL;

    if (client->gcc_server_disconnect_cb) {
	client->gcc_server_disconnect_cb(client);
    }
}

/*
 * Tries to connect to server with given timeout in milliseconds. -1 will
 * indefinitely try till a connection can be established
 */
int 
grpc_c_client_try_connect (grpc_c_client_t *client, long timeout)
{
    client->gcc_channel_state 
	= grpc_channel_check_connectivity_state(client->gcc_channel, 1);

    if (grpc_c_grpc_client_try_connect(timeout, gc_client_retry_timeout_cb, 
				       client, (void **)&client->gcc_retry_tag)) {
	gpr_log(GPR_ERROR, "Failed to retry");
	return 1;
    }

    return 0;
}

/*
 * Cancels connection attempt
 */
void 
grpc_c_client_cancel_connect (grpc_c_client_t *client)
{
    if (client && client->gcc_retry_tag) {
	grpc_c_grpc_client_cancel_try_connect((void *)client->gcc_retry_tag);
    }
}

void 
grpc_c_register_client_socket_create_callback (void (*fp)(int fd, 
							  const char *uri))
{
    grpc_c_grpc_set_client_socket_create_callback(fp);
}
