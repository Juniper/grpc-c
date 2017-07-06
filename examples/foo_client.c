/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include "foo.grpc-c.h"

/*
 * Takes as argument the socket name
 */
int 
main (int argc, char **argv) 
{
    if (argc < 2) {
	fprintf(stderr, "Too few arguments\n");
	exit(1);
    }

    /*
     * Initialize grpc-c library to be used with vanilla grpc
     */
    grpc_c_init(GRPC_THREADS, NULL);

    /*
     * Create a client object with client name as foo client to be talking to
     * a insecure server
     */
    grpc_c_client_t *client = grpc_c_client_init(argv[1], "foo client", NULL);

    /*
     * Create a hello request message and call RPC
     */
    foo__HelloRequest h;
    foo__hello_request__init(&h);
    foo__HelloReply *r;

    char str[BUFSIZ];
    snprintf(str, BUFSIZ, "world");
    h.name = str;

    grpc_c_context_t *context;
    /*
     * This will invoke a blocking RPC
     */
    int status = foo__greeter__say_hello(client, NULL, &h, &r, NULL, 0);
    printf("Got back: %s\n", r->message);
    printf("Finished with %d\n", status);

    status = foo__greeter__say_hello__sync(client, NULL, &context, &h, 0);
    context->gcc_stream->read(context, (void **)&r, 1000);

    printf("Got back now %s\n", r->message);
}
