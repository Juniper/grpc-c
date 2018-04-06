/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include "foo.grpc-c.h"

static grpc_c_server_t *test_server;

static void sigint_handler (int x) { 
    grpc_c_server_destroy(test_server);
    exit(0);
}

/*
 * This function gets invoked whenever say_hello RPC gets called
 */
void
foo__greeter__say_hello_cb (grpc_c_context_t *context)
{
    foo__HelloRequest *h;

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
    foo__HelloReply r;
    foo__hello_reply__init(&r);

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
}

/*
 * Takes socket path as argument
 */
int 
main (int argc, char **argv) 
{
    int i = 0;

    if (argc < 2) {
	fprintf(stderr, "Missing socket path argument\n");
	exit(1);
    }

    signal(SIGINT, sigint_handler);

    /*
     * Initialize grpc-c library to be used with vanilla gRPC
     */
    grpc_c_init(GRPC_THREADS, NULL);

    /*
     * Create server object
     */
    test_server = grpc_c_server_create(argv[1], NULL, NULL);
    if (test_server == NULL) {
	printf("Failed to create server\n");
	exit(1);
    }

    grpc_c_server_add_insecure_http2_port(test_server, "127.0.0.1:3000");

    /*
     * Initialize greeter service
     */
    foo__greeter__service_init(test_server);

    /*
     * Start server
     */
    grpc_c_server_start(test_server);

    /*
     * Blocks server to wait to completion
     */
    grpc_c_server_wait(test_server);
}
