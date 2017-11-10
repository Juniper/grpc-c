/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include "unary_sync.grpc-c.h"

static int success;

void 
unary_sync_client (grpc_c_client_t *client) 
{
    int status;
    grpc_c_context_t *context = NULL;

    /*
     * Create a hello request message and call RPC
     */
    unary_sync__HelloRequest h;
    unary_sync__hello_request__init(&h);
    unary_sync__HelloReply *r;

    char str[BUFSIZ];
    snprintf(str, BUFSIZ, "world");
    h.name = str;

    /*
     * This will invoke a blocking RPC
     */
    unary_sync__greeter__say_hello__sync(client, NULL, 0, &context, &h, -1);
    context->gcc_stream->read(context, (void **)&r, 0, -1);
    if (r) {
	printf("Got back: %s\n", r->message);
    }
    status = context->gcc_stream->finish(context, NULL, 0);
    printf("Finished with %d\n", status);
    success = 1;
}

int 
unary_sync_test (void) 
{
    return success ? 0 : 1;
}
