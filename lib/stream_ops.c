/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 */

#include "common/strextra.h"
#include "stream_ops.h"

/*
 * Figure out deadline to finish the operation. A timeout of -1 will
 * block till we get event back or the operation fails before that
 */
gpr_timespec 
gc_deadline_from_timeout (long timeout) 
{
    gpr_timespec deadline;

    if (timeout < 0) {
        deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
    } else if (timeout == 0) {
        deadline = gpr_time_0(GPR_CLOCK_REALTIME);
    } else {
        deadline = gpr_time_from_millis(timeout, GPR_CLOCK_REALTIME);
    }

    return deadline;
}

/*
 * Internal function to send available initial metadata to client
 */
int 
gc_send_initial_metadata_internal (grpc_c_context_t *context, int send)
{
    grpc_event ev;
    grpc_call_error e;

    if (context->gcc_meta_sent == 0) {
	if (grpc_c_ops_alloc(context, 1)) return 1;

	context->gcc_ops[context->gcc_op_count].op 
	    = GRPC_OP_SEND_INITIAL_METADATA;
	context->gcc_ops[context->gcc_op_count]
	    .data.send_initial_metadata.count 
	    = context->gcc_initial_metadata->count;

	if (context->gcc_initial_metadata->count > 0) {
	    context->gcc_ops[context->gcc_op_count].data
		.send_initial_metadata.metadata 
		= context->gcc_initial_metadata->metadata;
	}
	context->gcc_op_count++;

	if (send) {
	    gpr_mu_lock(context->gcc_lock);
	    context->gcc_event->gce_type = GRPC_C_EVENT_METADATA;
	    context->gcc_event->gce_refcount++;
	    e = grpc_call_start_batch(context->gcc_call, context->gcc_ops, 
				      context->gcc_op_count, context->gcc_event, 
				      NULL);
	    if (e == GRPC_CALL_OK) {
		context->gcc_op_count = 0;
	    } else {
		gpr_log(GPR_ERROR, "Failed to finish batch operations to "
			"send initial metadata - %d", e);
		gpr_mu_unlock(context->gcc_lock);
		return 1;
	    }

	    ev = grpc_completion_queue_pluck(context->gcc_cq, context->gcc_event, 
					     gpr_inf_future(GPR_CLOCK_REALTIME), 
					     NULL);
	    if (ev.type == GRPC_OP_COMPLETE) {
		context->gcc_event->gce_refcount--;
	    }
	    gpr_mu_unlock(context->gcc_lock);

	    if (ev.type == GRPC_OP_COMPLETE && ev.success) {
		context->gcc_op_count = 0;
		context->gcc_meta_sent = 1;
		return 0;
	    } else {
		return 1;
	    }
	} else {
	    context->gcc_meta_sent = 1;
	}
    }
    return 0;
}

/*
 * Reads data from stream
 */
int
gc_stream_read (grpc_c_context_t *context, void **output, long timeout)
{
    grpc_event ev;
    gpr_timespec deadline;
    int op_count = context->gcc_op_count;

    /*
     * Check if we have pending optional payload
     */
    if (context->gcc_payload == NULL) {
	if (context->gcc_read_event == NULL) {
	    context->gcc_read_event = malloc(sizeof(grpc_c_event_t));
	    if (context->gcc_read_event == NULL) {
		gpr_log(GPR_ERROR, "Failed to create read event");
		return GRPC_C_FAIL;
	    }
	    bzero(context->gcc_read_event, sizeof(grpc_c_event_t));
	}

	deadline = gc_deadline_from_timeout(timeout);

	if (context->gcc_read_event->gce_refcount == 0) {
	    if (grpc_c_ops_alloc(context, 1)) return 1;
	    gpr_mu_lock(context->gcc_lock);

	    context->gcc_ops[op_count].op = GRPC_OP_RECV_MESSAGE;
	    context->gcc_ops[op_count].data.recv_message = 
		&context->gcc_payload;

	    context->gcc_read_event->gce_type = GRPC_C_EVENT_READ;
	    context->gcc_read_event->gce_refcount++;

	    grpc_call_error e = grpc_call_start_batch(context->gcc_call, 
						      context->gcc_ops + op_count, 
						      1, context->gcc_read_event, 
						      NULL);
	    if (e != GRPC_CALL_OK) {
		context->gcc_read_event->gce_refcount--;
		gpr_mu_unlock(context->gcc_lock);
		gpr_log(GPR_ERROR, "Failed to finish read ops batch");
		return GRPC_C_FAIL;
	    }
	}

	ev = grpc_completion_queue_pluck(context->gcc_cq, 
					 context->gcc_read_event, 
					 deadline, NULL);
	if (ev.success == 0 && ev.type != GRPC_QUEUE_TIMEOUT) {
	    gpr_log(GPR_ERROR, "Failed to pluck read ops");
	    context->gcc_read_event->gce_refcount--;
	    gpr_mu_unlock(context->gcc_lock);
	    return GRPC_C_FAIL;
	} else if (ev.type == GRPC_QUEUE_TIMEOUT) {
	    gpr_log(GPR_DEBUG, "Read op queue timeout");
	    gpr_mu_unlock(context->gcc_lock);
	    return GRPC_C_TIMEOUT;
	}
	context->gcc_op_count = 0;
	context->gcc_read_event->gce_refcount--;
	gpr_mu_unlock(context->gcc_lock);
    }

    /*
     * Decode the received data. If we are a server, we decode using input
     * unpacker. For client, we decode using output unpacker
     */
    if (context->gcc_is_client) {
	*output = context->gcc_method_funcs->gcmf_output_unpacker(context, 
								  context->gcc_payload);
    } else {
	*output = context->gcc_method_funcs->gcmf_input_unpacker(context, 
								 context->gcc_payload);
    }

    if (context->gcc_payload) {
	grpc_byte_buffer_destroy(context->gcc_payload);
	context->gcc_payload = NULL;
    } else {
	/*
	 * If payload is NULL or we have invalid data, return NULL to the
	 * client so it can request for status
	 */
	*output = NULL;
    }

    return GRPC_C_OK;
}

/*
 * Write function for client
 */
int
gc_stream_write (grpc_c_context_t *context, void *input, long timeout)
{
    grpc_event ev;
    gpr_timespec deadline;
    int op_count;

    /*
     * If there is a pending write, return early
     */
    if (context->gcc_write_event 
	&& context->gcc_write_event->gce_refcount > 0) {
	return GRPC_C_WRITE_PENDING;
    }

    /*
     * Send initial metadata if we haven't already sent it
     */
    if (gc_send_initial_metadata_internal(context, 0)) {
	gpr_log(GPR_ERROR, "Failed to send initial metadata");
	return GRPC_C_FAIL;
    }

    if (context->gcc_write_event == NULL) {
	context->gcc_write_event = malloc(sizeof(grpc_c_event_t));
	if (context->gcc_write_event == NULL) {
	    gpr_log(GPR_ERROR, "Failed to create write event");
	    return GRPC_C_FAIL;
	}
	bzero(context->gcc_write_event, sizeof(grpc_c_event_t));
    }

    deadline = gc_deadline_from_timeout(timeout);

    if (context->gcc_write_event->gce_refcount == 0) {
	if (grpc_c_ops_alloc(context, 1)) return GRPC_C_FAIL;
	gpr_mu_lock(context->gcc_lock);

	op_count = context->gcc_op_count;
	if (context->gcc_is_client) {
	    context->gcc_method_funcs->gcmf_input_packer(input, 
					&context->gcc_ops_payload[op_count]);
	} else {
	    context->gcc_method_funcs->gcmf_output_packer(input, 
					&context->gcc_ops_payload[op_count]);
	}
	context->gcc_ops[op_count].op = GRPC_OP_SEND_MESSAGE;
	context->gcc_ops[op_count].data.send_message 
	    = context->gcc_ops_payload[op_count];
	context->gcc_op_count++;

	context->gcc_write_event->gce_type = GRPC_C_EVENT_WRITE;
	context->gcc_write_event->gce_refcount++;
	grpc_call_error e = grpc_call_start_batch(context->gcc_call, 
						  context->gcc_ops, 
						  context->gcc_op_count, 
						  context->gcc_write_event, 
						  NULL);
	if (e != GRPC_CALL_OK) {
	    context->gcc_write_event->gce_refcount--;
	    gpr_mu_unlock(context->gcc_lock);
	    gpr_log(GPR_ERROR, "Failed to finish write ops batch");
	    return GRPC_C_FAIL;
	}
    }

    ev = grpc_completion_queue_pluck(context->gcc_cq, context->gcc_write_event, 
				     deadline, NULL);
    if (ev.success == 0 
	|| (ev.type != GRPC_OP_COMPLETE && ev.type != GRPC_QUEUE_TIMEOUT)) {
	gpr_log(GPR_ERROR, "Failed to pluck write ops");
	context->gcc_write_event->gce_refcount--;
	gpr_mu_unlock(context->gcc_lock);
	return GRPC_C_FAIL;
    } else if (ev.type == GRPC_QUEUE_TIMEOUT) {
	gpr_log(GPR_DEBUG, "Write ops queue timeout");
	gpr_mu_unlock(context->gcc_lock);
	return GRPC_C_TIMEOUT;
    }
    context->gcc_op_count = 0;
    context->gcc_write_event->gce_refcount--;
    gpr_mu_unlock(context->gcc_lock);

    return GRPC_C_OK;
}

/*
 * This is used to send a write finish from client
 */
int 
gc_client_stream_write_done (grpc_c_context_t *context, long timeout)
{
    grpc_event ev;
    grpc_call_error e;
    gpr_timespec deadline;
    int op_count = context->gcc_op_count;

    if (context == NULL) return GRPC_C_FAIL;

    /*
     * We do not have to explicitly close write from server
     */
    if (!context->gcc_is_client) return GRPC_C_OK;

    /*
     * If we have already called write_done, return early
     */
    if (context->gcc_write_done_event 
	&& context->gcc_write_done_event->gce_refcount > 0) {
	gpr_log(GPR_DEBUG, "Called write done from client more than once");
	return GRPC_C_OK;
    }

    if (context->gcc_write_done_event == NULL) {
	context->gcc_write_done_event = malloc(sizeof(grpc_c_event_t));
	if (context->gcc_write_done_event == NULL) {
	    gpr_log(GPR_ERROR, "Failed to create write done event");
	    return GRPC_C_FAIL;
	}
	bzero(context->gcc_write_done_event, sizeof(grpc_c_event_t));
    }

    deadline = gc_deadline_from_timeout(timeout);

    if (context->gcc_write_done_event->gce_refcount == 0) {
	if (grpc_c_ops_alloc(context, 1)) return 1;
	gpr_mu_lock(context->gcc_lock);

	context->gcc_ops[op_count].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;

	context->gcc_write_done_event->gce_type = GRPC_C_EVENT_WRITE;
	context->gcc_write_done_event->gce_refcount++;
	grpc_call_error e = grpc_call_start_batch(context->gcc_call, 
						  context->gcc_ops + op_count, 1, 
						  context->gcc_write_done_event, 
						  NULL);
	if (e != GRPC_CALL_OK) {
	    context->gcc_write_done_event->gce_refcount--;
	    gpr_mu_unlock(context->gcc_lock);
	    gpr_log(GPR_ERROR, "Failed to finish write done ops batch");
	    return GRPC_C_FAIL;
	}
    }

    ev = grpc_completion_queue_pluck(context->gcc_cq, 
				     context->gcc_write_done_event, 
				     deadline, NULL);
    if (ev.success == 0 
	|| (ev.type != GRPC_OP_COMPLETE && ev.type != GRPC_QUEUE_TIMEOUT)) {
	gpr_log(GPR_ERROR, "Failed to pluck write done ops");
	context->gcc_write_done_event->gce_refcount--;
	gpr_mu_unlock(context->gcc_lock);
	return GRPC_C_FAIL;
    } else if (ev.type == GRPC_QUEUE_TIMEOUT) {
	gpr_log(GPR_DEBUG, "Write done op queue timeout");
	gpr_mu_unlock(context->gcc_lock);
	return GRPC_C_TIMEOUT;
    }
    context->gcc_op_count = 0;
    context->gcc_write_done_event->gce_refcount--;
    gpr_mu_unlock(context->gcc_lock);

    return GRPC_C_OK;
}

/*
 * Reader finish callback. Returns the status received into
 * context->gcc_status from previous call to grpc_c_client_request_status() 
 * when server sent NULL marking end of output
 */
int
gc_client_stream_finish (grpc_c_context_t *context, grpc_c_status_t *status)
{
    grpc_event ev;
    grpc_call_error e;
    int op_count = context->gcc_op_count;

    if (grpc_c_ops_alloc(context, 2)) return 1;

    context->gcc_ops[op_count].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op_count++;

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


    gpr_mu_lock(context->gcc_lock);
    e = grpc_call_start_batch(context->gcc_call, context->gcc_ops, 
			      context->gcc_op_count, context, NULL);
    if (e == GRPC_CALL_OK) {
	context->gcc_op_count = 0;
    } else {
	gpr_mu_unlock(context->gcc_lock);
	return 1;
    }

    ev = grpc_completion_queue_pluck(context->gcc_cq, context, 
				     gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    gpr_mu_unlock(context->gcc_lock);

    if (ev.type == GRPC_OP_COMPLETE && ev.success && status != NULL) {
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
 * Server stream finish function
 */
int
gc_server_stream_finish (grpc_c_context_t *context, grpc_c_status_t *status)
{
    int op_count = context->gcc_op_count;

    /*
     * If initial metadata is not sent, send inital metadata before sending 
     * close
     */
    if (gc_send_initial_metadata_internal(context, 0)) {
	gpr_log(GPR_ERROR, "Failed to send initial metadata");
	return GRPC_C_FAIL;
    }

    if (grpc_c_ops_alloc(context, 1)) {
	gpr_log(GPR_ERROR, "Failed to allocate memory for ops");
	return 1;
    }

    context->gcc_ops[context->gcc_op_count].op = GRPC_OP_SEND_STATUS_FROM_SERVER;
    context->gcc_status = status->gcs_code;
    context->gcc_ops[context->gcc_op_count].data.send_status_from_server.status 
	= context->gcc_status;
    context->gcc_ops[context->gcc_op_count].data.send_status_from_server
	.trailing_metadata_count = 0;
    context->gcc_ops[context->gcc_op_count].data
	.send_status_from_server.status_details 
	= (status->gcs_message != NULL) ? status->gcs_message : "";
    context->gcc_op_count++;

    gpr_mu_lock(context->gcc_lock);
    context->gcc_event->gce_type = GRPC_C_EVENT_WRITE_FINISH;
    context->gcc_event->gce_refcount++;
    grpc_call_error e = grpc_call_start_batch(context->gcc_call, 
					      context->gcc_ops, 
					      context->gcc_op_count - op_count, 
					      context->gcc_event, NULL);
    gpr_mu_unlock(context->gcc_lock);

    /*
     * If we are finishing while we have a write pending, do not mark for
     * cleanup rightaway
     */
    if (context->gcc_state == GRPC_C_WRITE_DATA_START) {
	context->gcc_cancelled = 1;
    } else {
	context->gcc_state = GRPC_C_SERVER_CONTEXT_CLEANUP;
    }

    if (e == GRPC_CALL_OK) {
	context->gcc_op_count = 0;
	return 0;
    } else {
	gpr_log(GPR_ERROR, "Failed to finish write batch ops");
	return 1;
    }
}

/*
 * Sends available initial metadata. Returns 0 on success and 1 on failure.
 * This function will block caller
 */
int 
grpc_c_send_initial_metadata (grpc_c_context_t *context) 
{
    return gc_send_initial_metadata_internal(context, 1);
}
