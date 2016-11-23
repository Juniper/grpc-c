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
gc_client_create_by_host (const char *host, const char *id)
{
    grpc_c_client_t *client;

    if (host == NULL || id == NULL) {
	gpr_log(GPR_ERROR, "Invalid hostname or client-id");
	return NULL;
    }

    client = malloc(sizeof(grpc_c_client_t));
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

    client->gcc_host = strdup(host);
    if (client->gcc_host == NULL) {
	gpr_log(GPR_ERROR, "Failed to copy hostname");
	grpc_c_client_free(client);
	return NULL;
    }

    /*
     * Create a channel using given hostname
     */
    client->gcc_channel = grpc_insecure_channel_create(host, NULL, NULL);
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

    return client;
}

/*
 * Creates a client instance using hostname
 */
static grpc_c_client_t *
gc_client_init_by_host (const char *hostname, const char *client_id) {
    if (hostname == NULL || client_id == NULL) return NULL;

    return gc_client_create_by_host(hostname, client_id);
}

/*
 * Creates and initializes client by building socket path from server name
 */
grpc_c_client_t *
grpc_c_client_init (const char *server_name, const char *client_id, 
		    grpc_channel_credentials *creds)
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

    client = gc_client_init_by_host(buf, client_id);

    if (client != NULL) {
	if (grpc_c_get_thread_pool()) {
	    gpr_cv_init(&client->gcc_callback_cv);
	}
    }
    return client;
}

/*
 * Free client instance and shutdown grpc
 */
void
grpc_c_client_free (grpc_c_client_t *client)
{
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

	if (client->gcc_host) free(client->gcc_host);

	if (client->gcc_channel) grpc_channel_destroy(client->gcc_channel);

	if (client->gcc_id) free(client->gcc_id);

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

	free(client);
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
    context->gcc_ops[op_count].data.recv_message = &context->gcc_payload;
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
    context->gcc_ops[op_count].data.recv_status_on_client.status_details_capacity 
	= &context->gcc_status_details_capacity;
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
 * Read callback for the client. Decodes the data received into gcc_payload
 * from previous call to gc_client_request_data(). Puts in a request to get
 * next message so data is available when the client calls reader->read
 */
static int
gc_client_reader_read (grpc_c_context_t *context, void **output)
{
    /*
     * Decode the received data
     */
    if (context->gcc_payload && 
	grpc_byte_buffer_length((grpc_byte_buffer *)context->gcc_payload) > 0) {
	*output = context->gcc_method_funcs->gcmf_output_unpacker(context, 
							context->gcc_payload);

	/*
	 * Free byte buffer
	 */
	grpc_byte_buffer_destroy(context->gcc_payload);
	context->gcc_payload = NULL;

	/*
	 * If server is streaming, request for next stream of message
	 */
	if (context->gcc_method->gcm_server_streaming) {
	    return gc_client_request_data(context);
	}
    } else {
	/*
	 * If payload is NULL or we have invalid data, return NULL to the
	 * client so it can request for status
	 */
	*output = NULL;
    }

    return 0;
}

/*
 * Reader finish callback. Returns the status received into
 * context->gcc_status from previous call to grpc_c_client_request_status() 
 * when server sent NULL marking end of output
 */
static int
gc_client_reader_finish (grpc_c_context_t *context, grpc_c_status_t *status)
{
    if (status != NULL) {
	status->gcs_code = context->gcc_status;

	if (context->gcc_status_details_capacity > 0) {
	    strlcpy(status->gcs_message, context->gcc_status_details, 
		    sizeof(status->gcs_message));
	} else {
	    status->gcs_message[0] = '\0';
	}
    }
    return context->gcc_status;
}

/*
 * Prepares context with read handler and calls the client callback. Returns 0
 * on success and 1 on failure. Caller of this function will have to assert
 * for success.
 */
static int
gc_prepare_client_callback (grpc_c_context_t *context)
{
    grpc_c_read_handler_t *read_handler;
    
    if (context == NULL) {
	return 0;
    }

    if (context->gcc_reader == NULL) {
	read_handler = malloc(sizeof(grpc_c_read_handler_t));
    } else {
	read_handler = context->gcc_reader;
    }

    if (read_handler == NULL) {
	return 0;
    }

    read_handler->read = &gc_client_reader_read;
    read_handler->finish = &gc_client_reader_finish;
    read_handler->free = context->gcc_method_funcs->gcmf_output_free;

    context->gcc_reader = read_handler;

    /*
     * Invoke callback with read handler if we have data so client can read.
     */
    if (context->gcc_method_funcs->gcmf_handler.gcmfh_client == NULL) {
	/*
	 * Free context and return failure if there is no client callback
	 */
	grpc_c_context_free(context);
	return 1;
    } else if (context->gcc_payload && 
	       grpc_byte_buffer_length((grpc_byte_buffer *)context->gcc_payload) 
	       > 0) {
	/*
	 * Call client callback if there is data
	 */
	context->gcc_method_funcs->gcmf_handler.gcmfh_client(context);

	/*
	 * If we have a non streaming server or we are done, we do not expect 
	 * anymore ops. We can safely free context
	 */
	if (context->gcc_method->gcm_server_streaming == 0) {
	    grpc_c_context_free(context);
	    context = NULL;
	}
    } else if (context->gcc_meta_sent == 0) {
	/*
	 * We are reusing gcc_meta_sent flag to check if we have requested for
	 * status. If there is no data (end of output from server) and we
	 * haven't already requested for status, request for status and set
	 * gcc_meta_sent flag indicating that we have requested for status in
	 * case of streaming server
	 */
	if (gc_client_request_status(context)) {
	    grpc_c_context_free(context);
	    return 1;
	}
	context->gcc_meta_sent = 1;
    } else {
	/*
	 * If we don't have data and we have already requested for status 
	 * and have a valid status, let the callback know
	 */
	if (context->gcc_status != GRPC_STATUS_UNAVAILABLE) {
	    context->gcc_method_funcs->gcmf_handler.gcmfh_client(context);
	}

	/*
	 * Free context if we are done
	 */
	if (context->gcc_state == GRPC_C_CLIENT_DONE) {
	    grpc_c_context_free(context);
	}
    }

    return 0;
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
		 * Get current channel connectivity state and compare it
		 * against the last state
		 */
		client = (grpc_c_client_t *) ev.tag;
		grpc_connectivity_state s 
		    = grpc_channel_check_connectivity_state(client->gcc_channel, 
							    0);

		/*
		 * If our current state is failure, our connection to server
		 * is dropped
		 */
		if (s == GRPC_CHANNEL_TRANSIENT_FAILURE || 
		    s == GRPC_CHANNEL_SHUTDOWN) {
		    if (client->gcc_server_disconnect_cb) {
			client->gcc_server_disconnect_cb(client);
		    }
		} else {
		    /*
		     * If our previous state is connecting and we are ready
		     * now, we just established a connection
		     */
		    if ((client->gcc_channel_state == GRPC_CHANNEL_IDLE 
			 || client->gcc_channel_state == GRPC_CHANNEL_CONNECTING) 
			&& s == GRPC_CHANNEL_READY) {
			if (client->gcc_server_connect_cb) {
			    client->gcc_server_connect_cb(client);
			}
		    }
		}

		client->gcc_channel_state = s;

		/*
		 * Watch for change in channel connectivity
		 */
		grpc_channel_watch_connectivity_state(client->gcc_channel, s, 
						      gpr_inf_future(GPR_CLOCK_REALTIME), 
						      cq, (void *) client);

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
    grpc_c_client_t *client = NULL;
    grpc_event ev;
    int shutdown = 0, timeout = 0, rc = 0;

    while (!shutdown && !timeout) {
	timeout = 0;
	ev = grpc_completion_queue_next(cq, ts, NULL);

	switch (ev.type) {
	    case GRPC_OP_COMPLETE:
		context = (grpc_c_context_t *)ev.tag;
		if (context) {
		    client = context->gcc_data.gccd_client;
		}
		GPR_ASSERT(gc_prepare_client_callback(context) == 0);
		break;
	    case GRPC_QUEUE_SHUTDOWN:
		grpc_completion_queue_destroy(cq);
		gpr_mu_lock(&client->gcc_lock);
		client->gcc_running_cb--;
		gpr_mu_unlock(&client->gcc_lock);
		/*
		 * If we are done executing all the callbacks, finish 
		 * shutdown
		 */
		if (grpc_c_get_thread_pool()) {
		    if (client->gcc_running_cb == 0 && client->gcc_shutdown) {
			gpr_cv_signal(&client->gcc_shutdown_cv);
		    }

		    gpr_cv_signal(&client->gcc_callback_cv);
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
 * Creates a context and fills initial operations for calling RPC and sending
 * initial metadata, data and request for data from server
 */
static grpc_c_context_t *
gc_client_prepare_ops (grpc_c_client_t *client, int sync UNUSED, void *input, 
		       void *tag, int client_streaming, 
		       int server_streaming, 
		       grpc_c_client_callback_t *cb, 
		       grpc_c_method_data_pack_t *input_packer, 
		       grpc_c_method_data_unpack_t *input_unpacker, 
		       grpc_c_method_data_free_t *input_free, 
		       grpc_c_method_data_pack_t *output_packer, 
		       grpc_c_method_data_unpack_t *output_unpacker, 
		       grpc_c_method_data_free_t *output_free)
{
    grpc_c_context_t *context = grpc_c_context_init(NULL, 1);
    if (context == NULL) {
	gpr_log(GPR_ERROR, "Failed to create context");
	return NULL;
    }

    context->gcc_method = malloc(sizeof(struct grpc_c_method_t));
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
    context->gcc_metadata->capacity += 1;
    context->gcc_metadata->count += 1;
    if (context->gcc_metadata->metadata != NULL) {
	context->gcc_metadata->metadata = realloc(context->gcc_metadata->metadata, 
						  context->gcc_metadata->capacity 
						  * sizeof(grpc_metadata));
    } else {
	context->gcc_metadata->metadata = malloc(sizeof(grpc_metadata));
    }

    if (context->gcc_metadata->metadata == NULL) {
	grpc_c_context_free(context);
	gpr_log(GPR_ERROR, "Failed to (re)allocate memory for metadata");
	return NULL;
    }
	
    context->gcc_metadata->metadata[context->gcc_metadata->count - 1].key 
	= "client-id";
    context->gcc_metadata->metadata[context->gcc_metadata->count - 1].value 
	= client->gcc_id;
    context->gcc_metadata->metadata[context->gcc_metadata->count - 1].value_length 
	= client->gcc_id ? strlen(client->gcc_id) : 0;

    /*
     * Send initial metadata with client-id
     */
    context->gcc_ops[op_count].op = GRPC_OP_SEND_INITIAL_METADATA;
    context->gcc_ops[op_count].data.send_initial_metadata.count = 1;
    context->gcc_ops[op_count].data.send_initial_metadata.metadata 
	= &context->gcc_metadata->metadata[context->gcc_metadata->count - 1];
    op_count++;

    /*
     * Encode data to be sent into wire format
     */
    input_packer(input, &context->gcc_ops_payload[op_count]);

    context->gcc_ops[op_count].op = GRPC_OP_SEND_MESSAGE;
    context->gcc_ops[op_count].data.send_message 
	= context->gcc_ops_payload[op_count];
    op_count++;

    context->gcc_ops[op_count].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op_count++;

    context->gcc_ops[op_count].op = GRPC_OP_RECV_INITIAL_METADATA;
    context->gcc_ops[op_count].data.recv_initial_metadata 
	= context->gcc_initial_metadata;
    op_count++;

    context->gcc_ops[op_count].op = GRPC_OP_RECV_MESSAGE;
    context->gcc_ops[op_count].data.recv_message = &context->gcc_payload;
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
	context->gcc_ops[op_count].data.recv_status_on_client.status_details_capacity 
	    = &context->gcc_status_details_capacity;
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
     * If we are async, we need a thread in background to process events
     * from completion queue. Otherwise we pluck in the same thread
     */
    if (grpc_c_get_thread_pool() && !client->gcc_shutdown && !sync) {
	grpc_c_thread_pool_add(grpc_c_get_thread_pool(), gc_run_rpc, 
			     (void *)context->gcc_cq);
    }
    gpr_mu_lock(&client->gcc_lock);
    client->gcc_running_cb++;
    gpr_mu_unlock(&client->gcc_lock);

    return context;
}

/*
 * Main function that handles streaming/async RPC calls. Returns 1 on error
 * and 0 on success. This return value is propagated to the client by the
 * autogenerated functions. Client is expected to check the return status of
 * these functions.
 */
int
grpc_c_client_request_async (grpc_c_client_t *client, const char *method,
			     void *input, void *tag, 
			     int client_streaming, int server_streaming, 
			     grpc_c_client_callback_t *cb, 
			     grpc_c_method_data_pack_t *input_packer, 
			     grpc_c_method_data_unpack_t *input_unpacker, 
			     grpc_c_method_data_free_t *input_free, 
			     grpc_c_method_data_pack_t *output_packer, 
			     grpc_c_method_data_unpack_t *output_unpacker, 
			     grpc_c_method_data_free_t *output_free)
{
    grpc_call_error e;
    int rc = 1;

    grpc_c_context_t *context = gc_client_prepare_ops(client, 0, input, tag, 
						      client_streaming, 
						      server_streaming, 
						      cb, input_packer, 
						      input_unpacker, 
						      input_free, 
						      output_packer, 
						      output_unpacker, 
						      output_free);
    if (context == NULL) {
	gpr_log(GPR_ERROR, "Failed to create context with async operations");
	return rc;
    }

    /*
     * Create call to the required RPC
     */
    context->gcc_call = grpc_channel_create_call(client->gcc_channel, 
						 NULL, 0, 
						 context->gcc_cq, method, 
						 client->gcc_host, 
						 gpr_inf_future(GPR_CLOCK_REALTIME), 
						 NULL);

    e = grpc_call_start_batch(context->gcc_call, context->gcc_ops, 
			      context->gcc_op_count, context, NULL);

    if (e == GRPC_CALL_OK) {
	context->gcc_op_count = 0;
	rc = 0;
    } else {
	gpr_log(GPR_ERROR, "Failed to finish batch operations on async call: "
		"%d", e);
	grpc_c_context_free(context);
	rc = 1;
    }

    return rc;
}

/*
 * Main function that handles sync/non-streaming RPC calls. Returns 1 on error
 * and 0 on success. Client is expected to check the return status of the
 * autogenerated functions which propagate return value of this function
 */
int
grpc_c_client_request_sync (grpc_c_client_t *client, const char *method, 
			    void *input, void **output, grpc_c_status_t *status, 
			    int client_streaming, int server_streaming, 
			    grpc_c_method_data_pack_t *input_packer, 
			    grpc_c_method_data_unpack_t *input_unpacker, 
			    grpc_c_method_data_free_t *input_free, 
			    grpc_c_method_data_pack_t *output_packer, 
			    grpc_c_method_data_unpack_t *output_unpacker, 
			    grpc_c_method_data_free_t *output_free, 
			    long timeout)
{
    grpc_call_error e;
    grpc_event ev;
    gpr_timespec tout;
    grpc_c_context_t *context = gc_client_prepare_ops(client, 1, input, NULL, 
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
	return 1;
    }
    
    context->gcc_call = grpc_channel_create_call(client->gcc_channel, 
						 NULL, 0, 
						 context->gcc_cq, method, 
						 client->gcc_host, 
						 gpr_inf_future(GPR_CLOCK_REALTIME), 
						 NULL);

    e = grpc_call_start_batch(context->gcc_call, context->gcc_ops, 
			      context->gcc_op_count, context, NULL);

    if (e == GRPC_CALL_OK) {
	context->gcc_op_count = 0;
    } else {
	grpc_c_context_free(context);
	gpr_log(GPR_ERROR, "Failed to finish batch operations on sync call: "
		"%d", e);
	return 1;
    }

    /*
     * In sync operations, we block till the event we are interested in is
     * available. If timeout is zero, then we block till call is executed
     */
    if (timeout == 0) {
	tout = gpr_inf_future(GPR_CLOCK_REALTIME);
    } else {
	tout = gpr_time_from_seconds(timeout, GPR_CLOCK_REALTIME);
    }

    ev = grpc_completion_queue_pluck(context->gcc_cq, context, tout, NULL);
    if (ev.type != GRPC_OP_COMPLETE) {
	gpr_log(GPR_ERROR, "Failed to pluck sync event");
	return 1;
    }

    /*
     * Decode the received data and point client provided pointer to this data
     */
    if (context->gcc_payload && 
	grpc_byte_buffer_length((grpc_byte_buffer *)context->gcc_payload) > 0) {
	*output = output_unpacker(context, context->gcc_payload);
    } else {
	*output = NULL;
    }

    /*
     * If we are given a pointer to status, fill it with return code from
     * server and message if any
     */
    if (status) {
	status->gcs_code = context->gcc_status;
	if (context->gcc_status_details_capacity > 0) {
	    strlcpy(status->gcs_message, context->gcc_status_details, 
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

    return 0;
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
