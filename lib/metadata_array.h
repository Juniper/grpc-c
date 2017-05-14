/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef GPRC_C_INTERNAL_METADATA_ARRAY_H
#define GRPC_C_INTERNAL_METADATA_ARRAY_H

#include <grpc-c/grpc-c.h>

/*
 * Initialize a metadata array
 */
void 
grpc_c_metadata_array_init (grpc_c_metadata_array_t *array); 

/*
 * Destroy a metadata array
 */
void 
grpc_c_metadata_array_destroy (grpc_c_metadata_array_t *array);

/*
 * Get metadata by key from given metadata array
 */
const char *
grpc_get_metadata_by_array (grpc_c_metadata_array_t *mdarray, const char *key);

/*
 * Insert provided key value pair to given metadata array and storage list
 */
int 
grpc_c_add_metadata_by_array (grpc_c_metadata_array_t *mdarray, 
			      char ***store, const char *key, 
			      const char *value);

#endif /* GRPC_C_INTERNAL_METADATA_ARRAY_H */
