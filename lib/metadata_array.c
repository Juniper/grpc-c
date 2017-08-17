/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <grpc-c/grpc-c.h>
#include "common/strextra.h"
#include "metadata_array.h"

/*
 * Initializes metadata array
 */
void 
grpc_c_metadata_array_init (grpc_c_metadata_array_t *array) 
{
    grpc_metadata_array_init(array);
}

/*
 * Destroys metadata array after destroying metadata
 */
void 
grpc_c_metadata_array_destroy (grpc_c_metadata_array_t *array)
{
    /*
     * Free metadata keyvalue pairs
     */
    while (array->count > 0) {
	grpc_slice_unref(array->metadata[array->count - 1].key);
	grpc_slice_unref(array->metadata[array->count - 1].value);

	array->count -= 1;
    }
    grpc_metadata_array_destroy(array);
}

/*
 * Searches given metadata array for key and returns the value. Will return
 * NULL if given key is not found
 */
const char *
grpc_get_metadata_by_array (grpc_c_metadata_array_t *mdarray, const char *key)
{
    size_t i;
    char *value = NULL;
    /*
     * Search the metadata array for key and if found, return the value
     */
    if (mdarray && mdarray->count > 0) {
	for (i = 0; i < mdarray->count; i++) {
	    if (grpc_slice_str_cmp(mdarray->metadata[i].key, key) == 0) {
		value = GRPC_SLICE_START_PTR(mdarray->metadata[i].value);
		value[GRPC_SLICE_LENGTH(mdarray->metadata[i].value)] = '\0';
		break;
	    }
	}
    }
    return value;
}

/*
 * Inserts given keyvalue pair into metadata array. Returns 0 on success and 1
 * on failure
 */
int 
grpc_c_add_metadata_by_array (grpc_c_metadata_array_t *mdarray, 
			      char ***store, const char *key, 
			      const char *value)
{
    if (key == NULL || value == NULL) {
	gpr_log(GPR_DEBUG, "Invalid key or value");
	return 0;
    }

    /*
     * Make space to hold metada
     */
    mdarray->capacity += 1;
    mdarray->count += 1;
    if (mdarray->metadata != NULL) {
	mdarray->metadata = gpr_realloc(mdarray->metadata, 
				    mdarray->capacity * sizeof(grpc_metadata));
	*store = gpr_realloc(*store, mdarray->capacity * sizeof(char *) * 2);
    } else {
	mdarray->metadata = gpr_malloc(sizeof(grpc_metadata));
	*store = gpr_malloc(sizeof(char *) * 2);
    }

    if (mdarray->metadata == NULL) {
	gpr_log(GPR_ERROR, "Failed to (re)allocate memory for metadata");
	return 1;
    }

    (*store)[(mdarray->count - 1) * 2] = gpr_strdup(key);
    (*store)[(mdarray->count - 1) * 2 + 1] = gpr_strdup(value);

    mdarray->metadata[mdarray->count - 1].key 
	= grpc_slice_from_static_string((*store)[(mdarray->count - 1) * 2]);
    mdarray->metadata[mdarray->count - 1].value 
	= grpc_slice_from_static_string((*store)[(mdarray->count - 1) * 2 + 1]);

    return 0;
}
