/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include "server_streaming.grpc-c.h"

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
    server_streaming__HelloReply *r;
    do {
	if (context->gcc_stream->read(context, (void **)&r, 0, -1)) {
	    printf("Failed to read\n");
	    exit(1);
	}

	if (r) {
	    printf("Got back: %s\n", r->message);
	}
    } while(r);

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
     * Create a client object with client name as server_streaming client to 
     * be talking to a insecure server
     */
    client = grpc_c_client_init(argv[1], "server streaming client", NULL, 
				NULL);

    /*
     * Create a hello request message and call RPC
     */
    server_streaming__HelloRequest h;
    server_streaming__hello_request__init(&h);

    char str[BUFSIZ];
    snprintf(str, BUFSIZ, "world");
    h.name = str;

    /*
     * This will invoke a async RPC
     */
    server_streaming__greeter__say_hello__async(client, NULL, 0, &h, &cb, 
						(void *)1);

    pthread_t thr;
    pthread_create(&thr, NULL, test_check, NULL);

    grpc_c_client_wait(client);
}
