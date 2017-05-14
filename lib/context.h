/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef GPRC_C_INTERNAL_CONTEXT_H
#define GRPC_C_INTERNAL_CONTEXT_H

#include <grpc-c/grpc-c.h>

/*
 * Allocate and initialize context object
 */
grpc_c_context_t *
grpc_c_context_init (struct grpc_c_method_t *method, int is_client);

/*
 * Destroy and free context
 */
void grpc_c_context_free (grpc_c_context_t *context);

/*
 * Allocate space for operations
 */
int grpc_c_ops_alloc (grpc_c_context_t *context, int count);

#endif /* GRPC_C_INTERNAL_CONTEXT_H */
