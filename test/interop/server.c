/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include <grpc-c/grpc-c.h>

#include "empty.grpc-c.h"
#include "test.grpc-c.h"

/*
 * RPC callbacks
 */

void 
grpc__testing__test_service__empty_call_cb (grpc_c_context_t *context)
{
    grpc_c_status_t status;
    Grpc__Testing__Empty *input;
    Grpc__Testing__Empty output;
    grpc__testing__empty__init(&output);

    status.gcs_code = 0;

    gpr_log(GPR_DEBUG, "Reading\n");
    GPR_ASSERT(context->gcc_stream->read(context, (void **)&input, 0, -1) 
	       == GRPC_C_OK);
    gpr_log(GPR_DEBUG, "Sending\n");
    GPR_ASSERT(context->gcc_stream->write(context, &output, 0, -1) 
	       == GRPC_C_OK);
    gpr_log(GPR_DEBUG, "Finishing\n");
    GPR_ASSERT(context->gcc_stream->finish(context, &status, 0) 
	       == GRPC_C_OK);
}

void 
grpc__testing__test_service__unary_call_cb (grpc_c_context_t *context)
{
}

void
grpc__testing__test_service__cacheable_unary_call_cb (grpc_c_context_t *context)
{
}

void 
grpc__testing__test_service__streaming_output_call_cb (grpc_c_context_t *context)
{
}

void 
grpc__testing__test_service__streaming_input_call_cb (grpc_c_context_t *context)
{
}

void 
grpc__testing__test_service__full_duplex_call_cb (grpc_c_context_t *context)
{
}

void
grpc__testing__test_service__half_duplex_call_cb (grpc_c_context_t *context)
{
}

void 
grpc__testing__test_service__unimplemented_call_cb (grpc_c_context_t *context)
{
}

void 
grpc__testing__unimplemented_service__unimplemented_call_cb (grpc_c_context_t *context)
{
}

void 
grpc__testing__reconnect_service__start_cb (grpc_c_context_t *context)
{
}

void 
grpc__testing__reconnect_service__stop_cb (grpc_c_context_t *context)
{
}

/*
 * Sets up things and runs the server
 */
static int 
test_setup (int port, int use_tls)
{
    int rc = 0;
    char addr[1024];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);

    grpc_c_init(GRPC_THREADS, NULL);

    grpc_c_server_t *server = grpc_c_server_create_by_host(addr, NULL, NULL);

    if (server == NULL) {
	gpr_log(GPR_DEBUG, "Failed to create server\n");
	return 1;
    }

    rc = grpc__testing__test_service__service_init(server);
    rc += grpc__testing__unimplemented_service__service_init(server);
    rc += grpc__testing__reconnect_service__service_init(server);

    if (rc > 0) {
	gpr_log(GPR_DEBUG, "Failed to initialize all services\n");
	return 1;
    }

    grpc_c_server_start(server);
    grpc_c_server_wait(server);
}

int 
main (int argc, char **argv)
{
    int port, use_tls;

    while (1) {
	int c, option_index = 0;
	static struct option long_options[] = {
	    {"port", required_argument, 0, 1},
	    {"use_tls", required_argument, 0, 2}, 
	    {0, 0, 0, 0}
	};

	c = getopt_long(argc, argv, "", long_options, &option_index);
	if (c == -1) {
	    break;
	}

	switch (c) {
	case 0:
	    break;
	case 1:
	    port = atoi(optarg);
	    break;
	case 2:
	    use_tls = 1;
	    break;
	}
    }

    gpr_log(GPR_DEBUG, "All running\n");
    return test_setup(port, use_tls);
}
