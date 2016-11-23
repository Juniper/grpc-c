/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <grpc-c/grpc-c.h>
#include "context.h"
#include "hooks.h"

/*
 * Create and initialize context
 */
grpc_c_context_t *
grpc_c_context_init (struct grpc_c_method_t *method, int is_client)
{
    grpc_c_context_t *context = malloc(sizeof(grpc_c_context_t));
    if (context == NULL) {
	return NULL;
    }
    
    context->gcc_method = method;
    context->gcc_op_count = 0;
    context->gcc_ops = NULL;
    context->gcc_payload = NULL;
    context->gcc_ops_payload = NULL;
    context->gcc_cq = NULL;
    context->gcc_deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
    context->gcc_call = NULL;
    context->gcc_meta_sent = 0;
    context->gcc_reader = NULL;
    context->gcc_writer = NULL;
    context->gcc_writer_resolve_cb = NULL;
    context->gcc_writer_resolve_args = NULL;
    context->gcc_status_details = NULL;
    context->gcc_status_details_capacity = 0;
    context->gcc_metadata = malloc(sizeof(grpc_metadata_array));
    if (context->gcc_metadata == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory in context for metadata");
	grpc_c_context_free(context);
	return NULL;
    }
    grpc_metadata_array_init(context->gcc_metadata);

    context->gcc_initial_metadata = malloc(sizeof(grpc_metadata_array));
    if (context->gcc_initial_metadata == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory in context for "
		"initial metadata");
	grpc_c_context_free(context);
	return NULL;
    }
    grpc_metadata_array_init(context->gcc_initial_metadata);

    context->gcc_trailing_metadata = malloc(sizeof(grpc_metadata_array));
    if (context->gcc_trailing_metadata == NULL) {
	gpr_log(GPR_ERROR, "Failed to allocate memory in context for "
		"trailing metadata");
	grpc_c_context_free(context);
	return NULL;
    }
    grpc_metadata_array_init(context->gcc_trailing_metadata);
    

    context->gcc_method_funcs = malloc(sizeof(grpc_c_method_funcs_t));
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

    return context;
}

/*
 * Free the context object
 */
void
grpc_c_context_free (grpc_c_context_t *context)
{
    int i;

    if (context->gcc_payload) grpc_byte_buffer_destroy(context->gcc_payload);

    if (context->gcc_metadata) {
	grpc_metadata_array_destroy(context->gcc_metadata);
	free(context->gcc_metadata);
    }

    if (context->gcc_initial_metadata) {
	grpc_metadata_array_destroy(context->gcc_initial_metadata);
	free(context->gcc_initial_metadata);
    }

    if (context->gcc_trailing_metadata) {
	grpc_metadata_array_destroy(context->gcc_trailing_metadata);
	free(context->gcc_trailing_metadata);
    }

    if (context->gcc_call) grpc_call_destroy(context->gcc_call);

    /*
     * Free ops payload when we are done with all the ops
     */
    if (context->gcc_ops) free(context->gcc_ops);

    if (context->gcc_ops_payload != NULL) {
	/*
	 * Do not free ops if someone is already waiting on them
	 * TODO: add log message and exit properly
	 */
	if (context->gcc_op_count != 0) exit(1);

	for (i = 0; i < context->gcc_op_capacity; i++) {
	    if (context->gcc_ops_payload[i] != NULL) {
		grpc_byte_buffer_destroy(context->gcc_ops_payload[i]);
	    }
	}
	free(context->gcc_ops_payload);
    }

    if (context->gcc_reader) free(context->gcc_reader);

    if (context->gcc_writer) free(context->gcc_writer);

    if (context->gcc_is_client && context->gcc_method) free(context->gcc_method);

    if (context->gcc_method_funcs) free(context->gcc_method_funcs);

    if (context->gcc_status_details) free(context->gcc_status_details);

    if (context->gcc_cq) {
	if (context->gcc_is_client) {
	    grpc_completion_queue_shutdown(context->gcc_cq);
	} else {
	    if (context->gcc_state != GRPC_C_SERVER_CALLBACK_WAIT) {
		grpc_completion_queue_shutdown(context->gcc_cq);
	    }
	}
    }

    free(context);
    context = NULL;
}

/*
 * Make room for new ops in the context.
 */
int
grpc_c_ops_alloc (grpc_c_context_t *context, int count)
{
    if (context->gcc_ops == NULL) {
	context->gcc_ops = malloc(sizeof(grpc_op) * count);
	if (context->gcc_ops == NULL) {
	    gpr_log(GPR_ERROR, "Failed to allocate memory for ops");
	    return 1;
	}
	memset(context->gcc_ops, 0, sizeof(grpc_op) * count);

	context->gcc_ops_payload = malloc(sizeof(grpc_byte_buffer *) * count);
	if (context->gcc_ops_payload == NULL) {
	    gpr_log(GPR_ERROR, "Failed to allocate memory for ops payload");
	    return 1;
	}
	memset(context->gcc_ops_payload, 0, sizeof(grpc_byte_buffer *) * count);

	context->gcc_op_capacity = count;
    } else {
	if (context->gcc_op_count + count > context->gcc_op_capacity) {
	    context->gcc_ops = realloc(context->gcc_ops, sizeof(grpc_op) * 
				       (count + context->gcc_op_capacity));
	    if (context->gcc_ops == NULL) {
		gpr_log(GPR_ERROR, "Failed to realloc memory for ops");
		return 1;
	    }
	    memset(context->gcc_ops + context->gcc_op_capacity, 0, 
		   sizeof(grpc_op) * count);

	    context->gcc_ops_payload = realloc(context->gcc_ops_payload, 
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
