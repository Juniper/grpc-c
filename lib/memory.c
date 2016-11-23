/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <grpc-c/grpc-c.h>

/*
 * User provided allocate and free functions
 */
static grpc_c_memory_alloc_func_t *alloc_fn = NULL;
static grpc_c_memory_free_func_t *free_fn = NULL;

/*
 * Signatures for protobuf allocate and free function callbacks
 */
typedef void *(protobuf_c_alloc_func_t)(void *allocator_data, size_t size);
typedef void (protobuf_c_free_func_t)(void *allocator_data, void *data);

/*
 * Sets allocate function callback
 */
void 
grpc_c_set_memory_alloc_function (grpc_c_memory_alloc_func_t *fn)
{
    alloc_fn = fn;
}

/*
 * Sets free function callback
 */
void 
grpc_c_set_memory_free_function (grpc_c_memory_free_func_t *fn)
{
    free_fn = fn;
}

/*
 * Takes pointer to allocator and fills alloc, free functions if available.
 * Else return NULL
 */
ProtobufCAllocator *
grpc_c_get_protobuf_c_allocator (grpc_c_context_t *context, 
				 ProtobufCAllocator *allocator)
{
    if (alloc_fn && free_fn && allocator) {
	allocator->alloc = (protobuf_c_alloc_func_t *)alloc_fn;
	allocator->free = (protobuf_c_free_func_t *)free_fn;

	if (context) {
	    allocator->allocator_data = (void *)context;
	} else {
	    allocator->allocator_data = NULL;
	}

	return allocator;
    }

    return NULL;
}
