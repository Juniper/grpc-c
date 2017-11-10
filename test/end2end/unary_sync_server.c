/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include "unary_sync.grpc-c.h"

static int success = 0;

void 
unary_sync__greeter__say_hello_cb (grpc_c_context_t *context)
{
    unary_sync__HelloRequest *h;

    /*
     * Read incoming message into h
     */
    if (context->gcc_stream->read(context, (void **)&h, 0, -1)) {
	printf("Failed to read data from client\n");
	exit(1);
    }

    /*
     * Create a reply
     */
    unary_sync__HelloReply r;
    unary_sync__hello_reply__init(&r);

    char buf[1024];
    buf[0] = '\0';
    snprintf(buf, 1024, "hello, ");
    strcat(buf, h->name);
    r.message = buf;

    /*
     * Write reply back to the client
     */
    if (!context->gcc_stream->write(context, &r, 0, -1)) {
        printf("Wrote hello world to %s\n", grpc_c_get_client_id(context));
    } else {
        printf("Failed to write\n");
        exit(1);
    }

    grpc_c_status_t status;
    status.gcs_code = 0;

    /*
     * Finish response for RPC
     */
    if (context->gcc_stream->finish(context, &status, 0)) {
        printf("Failed to write status\n");
        exit(1);
    }
    success++;
}

void 
unary_sync_server (grpc_c_server_t *server) 
{
    unary_sync__greeter__service_init(server);
}

int 
unary_sync_test (void) 
{
    return success ? 0 : 1;
}
