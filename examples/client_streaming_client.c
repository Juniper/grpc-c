/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include "client_streaming.grpc-c.h"

static grpc_c_client_t *client;
static int done = 0;

static void *
test_check (void *arg)
{
    while (done == 0) {};

    grpc_c_client_free(client);
    grpc_c_shutdown();

    return NULL;
}

static void 
cb (grpc_c_context_t *context, void *tag, int success)
{
    int i;

    /*
     * Create a hello request message and call RPC
     */
    client_streaming__HelloRequest h;
    client_streaming__hello_request__init(&h);

    char str[BUFSIZ];
    snprintf(str, BUFSIZ, "world");
    h.name = str;

    /*
     * Stream input messages and end of stream
     */
    for (i = 0; i < 10; i++) {
	if (context->gcc_stream->write(context, &h, 0, -1)) {
	    printf("Failed to write\n");
	    exit(1);
	}
    }
    context->gcc_stream->write_done(context, 0, -1);

    int status = context->gcc_stream->finish(context, NULL, 0);
    printf("Finished with %d\n", status);
    done = 1;
}

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
     * Create a client object with client name as client_streaming client to 
     * be talking to a insecure server
     */
    client = grpc_c_client_init(argv[1], "client streaming client", NULL, 
				NULL);

    /*
     * This will invoke a async RPC
     */
    client_streaming__greeter__say_hello__async(client, NULL, 0, NULL, &cb, (void *)1);

    pthread_t thr;
    pthread_create(&thr, NULL, test_check, NULL);

    grpc_c_client_wait(client);
}
