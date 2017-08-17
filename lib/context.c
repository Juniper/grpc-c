/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <grpc-c/grpc-c.h>
#include "context.h"
#include "hooks.h"
#include "trace.h"
#include "metadata_array.h"

/*
 * Create and initialize context
 */
grpc_c_context_t *
grpc_c_context_init (struct grpc_c_method_t *method, int is_client)
{
    grpc_c_context_t *context = gpr_malloc(sizeof(grpc_c_context_t));
    if (context == NULL) {
	return NULL;
    }
    bzero(context, sizeof(grpc_c_context_t));
    
    context->gcc_method = method;
    context->gcc_deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
    context->gcc_client_cancel = 1;
    context->gcc_metadata = gpr_malloc(sizeof(grpc_metadata_array));
    if (context->gcc_metadata == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory in context for metadata");
	grpc_c_context_free(context);
	return NULL;
    }
    grpc_metadata_array_init(context->gcc_metadata);

    context->gcc_initial_metadata = gpr_malloc(sizeof(grpc_metadata_array));
    if (context->gcc_initial_metadata == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory in context for "
		"initial metadata");
	grpc_c_context_free(context);
	return NULL;
    }
    grpc_metadata_array_init(context->gcc_initial_metadata);

    context->gcc_trailing_metadata = gpr_malloc(sizeof(grpc_metadata_array));
    if (context->gcc_trailing_metadata == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory in context for "
		"trailing metadata");
	grpc_c_context_free(context);
	return NULL;
    }
    grpc_metadata_array_init(context->gcc_trailing_metadata);
    

    context->gcc_method_funcs = gpr_malloc(sizeof(grpc_c_method_funcs_t));
    if (context->gcc_method_funcs == NULL) {
	grpc_c_context_free(context);
	return NULL;
    }
    bzero(context->gcc_method_funcs, sizeof(grpc_c_method_funcs_t));

    if (is_client == 0) {
	context->gcc_is_client = 0;
	context->gcc_data.gccd_client = NULL;
    } else {
	context->gcc_data.gccd_server = NULL;
	context->gcc_is_client = 1;
    }

    /*
     * Allocate memory for stream handler
     */
    context->gcc_stream = gpr_malloc(sizeof(grpc_c_stream_handler_t));
    if (context->gcc_stream == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory for stream handler");
	grpc_c_context_free(context);
	return NULL;
    }
    bzero(context->gcc_stream, sizeof(grpc_c_stream_handler_t));

    return context;
}

/*
 * Frees key value pairs stored in metadata array
 */
static void 
gc_metadata_storage_free (char **store, size_t count)
{
    size_t i;

    if (store && count > 0) {
	for (i = 0; i < count; i++) {
	    gpr_free(store[2 * i]);
	    gpr_free(store[2 * i + 1]);
	}
    }
    gpr_free(store);
}

/*
 * Free the context object
 */
void
grpc_c_context_free (grpc_c_context_t *context)
{
    int i;

    if (context->gcc_metadata) {
	/*
	 * In case of client, we take a copy of key value pairs into metadata
	 * array before sending to server. We have to free that data before
	 * destroying metadata array
	 */
	if (context->gcc_is_client) {
	    gc_metadata_storage_free(context->gcc_metadata_storage, 
				     context->gcc_metadata->count);
	}
	grpc_metadata_array_destroy(context->gcc_metadata);
	gpr_free(context->gcc_metadata);
    }

    /*
     * In server, we take copy of key value pairs of initial and trailing
     * metadata before sending over wire. We have to free them before
     * destroying these arrays
     */
    if (context->gcc_initial_metadata) {
	if (!context->gcc_is_client) {
	    gc_metadata_storage_free(context->gcc_initial_metadata_storage, 
				     context->gcc_initial_metadata->count);
	}
	grpc_metadata_array_destroy(context->gcc_initial_metadata);
	gpr_free(context->gcc_initial_metadata);
    }

    if (context->gcc_trailing_metadata) {
	if (!context->gcc_is_client) {
	    gc_metadata_storage_free(context->gcc_trailing_metadata_storage, 
				     context->gcc_trailing_metadata->count);
	}
	grpc_metadata_array_destroy(context->gcc_trailing_metadata);
	gpr_free(context->gcc_trailing_metadata);
    }

    if (context->gcc_call) grpc_call_destroy(context->gcc_call);

    /*
     * Free ops payload when we are done with all the ops
     */
    if (context->gcc_ops) gpr_free(context->gcc_ops);

    if (context->gcc_ops_payload != NULL) {
	/*
	 * Log if we are freeing context that is still in use
	 */
	if (context->gcc_op_count != 0) {
	    gpr_log(GPR_ERROR, "Freeing context that is still in use");
	}

	for (i = 0; i < context->gcc_op_capacity; i++) {
	    if (context->gcc_ops_payload[i] != NULL) {
		grpc_byte_buffer_destroy(context->gcc_ops_payload[i]);
	    }
	}
	gpr_free(context->gcc_ops_payload);
    }

    if (context->gcc_reader) gpr_free(context->gcc_reader);

    if (context->gcc_writer) gpr_free(context->gcc_writer);

    if (context->gcc_is_client && context->gcc_method) {
	gpr_free(context->gcc_method);
    }

    if (context->gcc_method_funcs) gpr_free(context->gcc_method_funcs);

    grpc_slice_unref(context->gcc_status_details);

    if (context->gcc_cq) {
	if (context->gcc_is_client) {
	    grpc_completion_queue_shutdown(context->gcc_cq);
	} else {
	    if (context->gcc_state != GRPC_C_SERVER_CALLBACK_WAIT) {
		grpc_completion_queue_shutdown(context->gcc_cq);
	    }
	}
    }

    /*
     * If we are freeing this context without using it, free corresponding
     * event and mutex memory
     */
    if (context->gcc_state == GRPC_C_SERVER_CALLBACK_WAIT 
	|| !grpc_c_get_thread_pool()) {
	if (context->gcc_lock) gpr_free(context->gcc_lock);
    }

    /*
     * If this is context from client, remove this from list of contexts that
     * we are tracking
     */
    if (context->gcc_is_client) {
	LIST_REMOVE(context, gcc_list);
    }

    if (context->gcc_payload) grpc_byte_buffer_destroy(context->gcc_payload);

    /*
     * Event types when batching operations
     */
    context->gcc_event.gce_type = GRPC_C_EVENT_RPC_INIT;
    context->gcc_read_event.gce_type = GRPC_C_EVENT_READ;
    context->gcc_write_event.gce_type = GRPC_C_EVENT_WRITE;
    context->gcc_write_done_event.gce_type = GRPC_C_EVENT_WRITE_FINISH;
    context->gcc_recv_close_event.gce_type = GRPC_C_EVENT_RECV_CLOSE;

    /*
     * Mark event tag corresponding to this context for cleanup
    if (context->gcc_event) {
	if (context->gcc_state == GRPC_C_SERVER_CALLBACK_WAIT) {
	    gpr_free(context->gcc_event);
	} else {
	    context->gcc_event->gce_type = GRPC_C_EVENT_CLEANUP;
	    context->gcc_event.gce_data = NULL;
	}
    }
     */

    gpr_free(context);
}

/*
 * Make room for new ops in the context.
 */
int
grpc_c_ops_alloc (grpc_c_context_t *context, int count)
{
    if (context->gcc_ops == NULL) {
	context->gcc_ops = gpr_malloc(sizeof(grpc_op) * count);
	if (context->gcc_ops == NULL) {
	    gpr_log(GPR_ERROR, "Failed to allocate memory for ops");
	    return 1;
	}
	memset(context->gcc_ops, 0, sizeof(grpc_op) * count);

	context->gcc_ops_payload = gpr_malloc(sizeof(grpc_byte_buffer *) * count);
	if (context->gcc_ops_payload == NULL) {
	    gpr_log(GPR_ERROR, "Failed to allocate memory for ops payload");
	    return 1;
	}
	memset(context->gcc_ops_payload, 0, sizeof(grpc_byte_buffer *) * count);

	context->gcc_op_capacity = count;
    } else {
	if (context->gcc_op_count + count > context->gcc_op_capacity) {
	    context->gcc_ops = gpr_realloc(context->gcc_ops, sizeof(grpc_op) * 
					   (count + context->gcc_op_capacity));
	    if (context->gcc_ops == NULL) {
		gpr_log(GPR_ERROR, "Failed to realloc memory for ops");
		return 1;
	    }
	    memset(context->gcc_ops + context->gcc_op_capacity, 0, 
		   sizeof(grpc_op) * count);

	    context->gcc_ops_payload = gpr_realloc(context->gcc_ops_payload, 
						   sizeof(grpc_byte_buffer *) * 
						   (count + context->gcc_op_capacity));
	    if (context->gcc_ops_payload == NULL) {
		gpr_log(GPR_ERROR, "Failed to realloc memory for ops payload");
		return 1;
	    }
	    memset(context->gcc_ops_payload + context->gcc_op_capacity, 0, 
		   sizeof(grpc_byte_buffer *) * count);

	    context->gcc_op_capacity += count;
	}
    }

    return 0;
}

/*
 * Extracts the value for a key from the metadata array. Returns NULL if given
 * key is not present
 */
const char *
grpc_c_get_metadata_by_key (grpc_c_context_t *context, const char *key)
{
    if (context) {
	return grpc_get_metadata_by_array(context->gcc_metadata, key);
    }
    return NULL;
}

/*
 * Returns value for given key fro initial metadata array
 */
const char *
grpc_c_get_initial_metadata_by_key (grpc_c_context_t *context, const char *key)
{
    if (context) {
	return grpc_get_metadata_by_array(context->gcc_initial_metadata, key);
    }
    return NULL;
}

/*
 * Returns value for given key from trailing metadata array
 */
const char *
grpc_c_get_trailing_metadata_by_key (grpc_c_context_t *context, 
				     const char *key)
{
    if (context) {
	return grpc_get_metadata_by_array(context->gcc_trailing_metadata, key);
    }
    return NULL;
}

/*
 * Adds given key value pair to metadata array. Returns 0 on success and 1 on
 * failure
 */
int 
grpc_c_add_metadata (grpc_c_context_t *context, const char *key, 
		     const char *value)
{
    return grpc_c_add_metadata_by_array(context->gcc_metadata, 
					&context->gcc_metadata_storage, key, 
					value);
}

/*
 * Adds given key value pair to initial metadata array. Returns 0 on success
 * and 1 on failure
 */
int 
grpc_c_add_initial_metadata (grpc_c_context_t *context, const char *key, 
			     const char *value)
{
    return grpc_c_add_metadata_by_array(context->gcc_initial_metadata, 
					&context->gcc_initial_metadata_storage, 
					key, value);
}

/*
 * Adds given key value pair to trailing metadata array. Returns 0 on success
 * and 1 on failure
 */
int 
grpc_c_add_trailing_metadata (grpc_c_context_t *context, const char *key, 
			      const char *value)
{
    return grpc_c_add_metadata_by_array(context->gcc_trailing_metadata, 
					&context->gcc_trailing_metadata_storage, 
					key, value);
}
