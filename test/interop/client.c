/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include <grpc-c/grpc-c.h>

#include "common/strextra.h"

#include "empty.grpc-c.h"

static int request_stream_sizes[4] = {27182, 8, 1828, 45904};
static int response_stream_sizes[4] = {31415, 9, 2653, 58979};

/*
 * Returns metadata with requested compression set
 */
static 
grpc_metadata get_metadata_with_compression (grpc_compression_algorithm algo)
{
    const char *algorithm_name = NULL;
    grpc_metadata *md = NULL;

    md = malloc(sizeof(grpc_metadata));
    if (md == NULL) {
	gpr_log(GPR_ERROR, "Failed to get memory for metadata");
	abort();
    }

    /*
     * Get algorithm name for given algo
     */
    if (!grpc_compression_algorithm_name(algo, &algorithm_name)) {
	gpr_log(GPR_ERROR, "Failed to get algorithm name");
	abort();
    }
    GPR_ASSERT(algorithm_name != NULL);

    grpc_metadata md1[1] = {{grpc_slice_from_static_string(
			     GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY), 
			     grpc_slice_from_static_string(algorithm_name), 
			     0, {NULL, NULL, NULL, NULL}}};
    memcpy(md, md1, sizeof(grpc_metadata));

    return md;
}

/*
 * This test verifies that implementations support zero-size messages.
 * Ideally, client implementations would verify that the request and response
 * were zero bytes serialized, but this is generally prohibitive to perform,
 * so is not required.
 */
void 
empty_unary_fn (grpc_c_client_t *client) 
{
    Grpc__Testing__Empty input;
    Grpc__Testing__Empty *output;
    grpc_c_status_t status;
    long timeout = -1;

    grpc__testing__empty__init(&input);

    int rc = grpc__testing__test_service__empty_call(client, NULL, 0, &input, 
						     &output, &status, timeout);
    printf("%d\n", rc);
    GPR_ASSERT(rc == 0);
    GPR_ASSERT(output != NULL);
}

void 
cacheable_unary_fn (grpc_c_client_t *client)
{
    char data[1024];
    grpc_c_metadata_array_t mdarray;
    Grpc__Testing__SimpleRequest request;
    Grpc__Testing__SimpleResponse *response1;
    Grpc__Testing__SimpleResponse *response2;
    Grpc__Testing__SimpleResponse *response3;
    uint32_t initial_md_flags = 0;
    grpc_c_status_t status;

    gpr_timespec ts = gpr_now(GPR_CLOCK_PRECISE);
    snprintf(data, sizeof(data), "%lu", ts.tv_nsec);

    grpc__testing__SimpleRequest__init(&request);
    grpc__testing__Payload__init(&request->payload);

    /*
     * We use gRPC metadata APIs to create our metadata
     */
    grpc_metadata metadata[1] = {{grpc_slice_from_static_string("x-user-ip"), 
				  grpc_slice_from_static_string("1.2.3.4"), 
				  0, {NULL, NULL, NULL, NULL}}};

    /*
     * Copy timestamp to bytes field of payload
     */
    request.payload.body.data = malloc(strlen(data));
    if (request.payload.body.data) {
	memcpy(request.payload.body.data, data, strlen(data));
	request.payload.body.len = strlen(data);
	request.payload.has_body = 1;
    }

    /*
     * Set cacheable flag
     */
    initial_md_flags |= GRPC_INITIAL_METADATA_CACHEABLE_REQUEST;

    gpr_log(GPR_DEBUG, "Sending RPC with cacheable response");

    /*
     * Call cacheable unary method and check return status
     */
    GPR_ASSERT(grpc__testing__test_service__cacheable_unary_call(client, metadata, 
								 initial_md_flags, 
								 &request, 
								 &response1, NULL, 
								 -1) == GRPC_C_OK);
    gpr_log(GPR_DEBUG, "response 1 payload: %s", response1->payload->body->data);


    /*
     * Create another call with same parameters
     */
    GPR_ASSERT(grpc__testing__test_service__cacheable_unary_call(client, metadata, 
								 initial_md_flags, 
								 &request, 
								 &response2, NULL, 
								 -1) == GRPC_C_OK);
    gpr_log(GPR_DEBUG, "response 2 payload: %s", response2->payload->body->data);

    GPR_ASSERT(streq(response2->payload->body, response1->payload->body->data));

    /*
     * Modifying request so we don't get a cache hit
     */
    ts = gpr_now(GPR_CLOCK_PRECISE);
    snprintf(data, sizeof(data), "%lu", ts.tv_nsec);
    memcpy(request.payload.body.data, data, strlen(data));

    GPR_ASSERT(grpc__testing__test_service__cacheable_unary_call(client, metadata, 
								 initial_md_flags, 
								 &request, 
								 &response3, NULL, 
								 -1) == GRPC_C_OK);
    gpr_log(GPR_DEBUG, "response 3 payload: %s", response3->payload->body->data);

    GPR_ASSERT(!streq(response3->payload->body, response1->payload->body->data));

    /*
     * TODO: free memory
     */
}

/*
 * This test verifies unary calls succeed in sending messages, and touches on
 * flow control (even if compression is enabled on the channel).
 */
void 
large_unary_fn (grpc_c_client_t *client) 
{
    Grpc__Testing__SimpleRequest request;
    Grpc__Testing__SimpleResponse *response;

    grpc__testing__SimpleRequest__init(&request);

    /*
     * Fill response_size and request payload
     */
    request.response_size = 314159;
    request.payload.body.data = malloc(271828);
    if (request.payload.body.data) {
	bzero(request.payload.body.data);
	request.payload.has_body = 1;
    }

    gpr_log(GPR_DEBUG, "Sending RPC with large payload");
    GPR_ASSERT(grpc__testing__test_service__unary_call(client, NULL, 0, 
						       &request, &response, 
						       NULL, -1) == GRPC_C_OK);

    gpr_log(GPR_DEBUG, "response payload: %s", response->payload->body->data);

    GPR_ASSERT(response->payload->body->len == 314159);
    char buf[314159];
    bzero(buf, sizeof(buf));
    GPR_ASSERT(memcmp(response->payload->body->data, buf, 314159));

    /*
     * TODO: free data
     */
}

/*
 * This test verifies the client can compress unary messages by sending two
 * unary calls, for compressed and uncompressed payloads. It also sends an
 * initial probing request to verify whether the server supports the
 * CompressedRequest feature by checking if the probing call fails with an
 * INVALID_ARGUMENT status
 */
int 
client_compressed_unary_fn (grpc_c_client_t *client)
{
    Grpc__Testing__SimpleRequest request;
    Grpc__Testing__SimpleResponse *response;
    grpc_c_context_t *context = NULL;
    grpc_c_status_t status;
    const char *algorithm_name = NULL;

    grpc__testing__SimpleRequest__init(&request);

    /*
     * Get algorithm name for GRPC_COMPRESS_NONE
     */
    if (!grpc_compression_algorithm_name(GRPC_COMPRESS_NONE, &algorithm_name)) {
	gpr_log(GPR_ERROR, "Failed to get algorithm name");
	abort();
    }
    GPR_ASSERT(algorithm_name != NULL);

    grpc_metadata md1[1] = {{grpc_slice_from_static_string(
			     GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY), 
			     grpc_slice_from_static_string(algorithm_name), 
			     0, {NULL, NULL, NULL, NULL}}};
    
    /*
     * Fill response_size and request payload
     */
    request.response_size = 314159;
    request.payload.body.data = malloc(271828);
    if (request.payload.body.data) {
	bzero(request.payload.body.data);
	request.payload.has_body = 1;
    }
    request.expect_compressed = 1;
    gpr_log(GPR_DEBUG, "Sending probe for compressed unary request.");

    GPR_ASSERT(grpc__testing__test_service__unary_call(client, md, 0, 
						       &request, &response, 
						       &status, -1) == GRPC_C_OK);

    /*
     * Server should return invalid argument ideally. 3 is the status code for 
     * invalid argument from status_code_enum.h
     */
    if (status.gcs_code != 3) {
	gpr_log(GPR_DEBUG, "Compressed unary request probe failed");
	return false;
    }

    gpr_log(GPR_DEBUG, "Compressed unary request probe succeeded. Proceeding.");

    /*
     * Create one request valid request without compression. We need access to
     * context here so we can check compression algorithm
     */
    gpr_log(GPR_DEBUG, "Sending compressed unary request compression=off");
    request.expect_compressed = 0;
    GPR_ASSERT(grpc__testing__test_service__unary_call__sync(client, md1, 0, 
							     &context, &request, 
							     -1) == GRPC_C_OK);
    GPR_ASSERT(context->gcc_stream_read(context, (void **)&response, 0, -1) 
	       == GRPC_C_OK);
    /*
     * Make sure data is uncompressed
     */
    GPR_ASSERT(!(grpc_call_test_only_get_message_flags(context->gcc_call) 
		 & GRPC_WRITE_INTERNAL_COMPRESS));
    status = context->gcc_stream->finish(context, NULL, 0);

    /*
     * Check payload
     */
    GPR_ASSERT(response->payload->body->len == 314159);
    char buf[314159];
    bzero(buf, sizeof(buf));
    GPR_ASSERT(memcmp(response->payload->body->data, buf, 314159));

    /*
     * Create request with compression and check flags
     */
    request.expect_compressed = 1;

    /*
     * Get algorithm name for GRPC_COMPRESS_GZIP
     */
    if (!grpc_compression_algorithm_name(GRPC_COMPRESS_GZIP, &algorithm_name)) {
	gpr_log(GPR_ERROR, "Failed to get algorithm name");
	abort();
    }
    GPR_ASSERT(algorithm_name != NULL);

    grpc_metadata md2[1] = {{grpc_slice_from_static_string(
			     GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY), 
			     grpc_slice_from_static_string(algorithm_name), 
			     0, {NULL, NULL, NULL, NULL}}};
    GPR_ASSERT(grpc__testing__test_service__unary_call__sync(client, md2, 0, 
							     &context, &request, 
							     -1) == GRPC_C_OK);
    GPR_ASSERT(context->gcc_stream_read(context, (void **)&response, 0, -1) 
	       == GRPC_C_OK);

    /*
     * Make sure data is compressed and payload is all zeros
     */
    GPR_ASSERT(grpc_call_test_only_get_compression_algorithm(context->gcc_call) 
	       != GRPC_COMPRESS_NONE);
    GPR_ASSERT((grpc_call_test_only_get_message_flags(context->gcc_call) 
		& GRPC_WRITE_INTERNAL_COMPRESS));
    GPR_ASSERT(response->payload->body->len == 314159);
    GPR_ASSERT(memcmp(response->payload->body->data, buf, 314159));

    status = context->gcc_stream->finish(context, NULL, 0);

    /*
     * TODO: free
     */

    return 0;
}

/*
 * This test verifies the server can compress unary messages. It sends two
 * unary requests, expecting the server's response to be compressed or not
 * according to the response_compressed boolean.
 */
int 
server_compressed_unary (grpc_c_client_t *client)
{
    Grpc__Testing__SimpleRequest request;
    Grpc__Testing__SimpleResponse *response;
    grpc_c_context_t *context = NULL;
    grpc_c_status_t status;

    grpc__testing__SimpleRequest__init(&request);

    /*
     * Create one request valid request without compression. We need access to
     * context here so we can check compression algorithm
     */
    gpr_log(GPR_DEBUG, "Sending unary request with server compression=off");
    request.response_compressed = 0;
    GPR_ASSERT(grpc__testing__test_service__unary_call__sync(client, NULL, 0, 
							     &context, &request, 
							     -1) == GRPC_C_OK);
    GPR_ASSERT(context->gcc_stream_read(context, (void **)&response, 0, -1) 
	       == GRPC_C_OK);
    /*
     * Make sure data is uncompressed
     */
    GPR_ASSERT(!(grpc_call_test_only_get_message_flags(context->gcc_call) 
		 & GRPC_WRITE_INTERNAL_COMPRESS));
    status = context->gcc_stream->finish(context, NULL, 0);

    /*
     * Check payload
     */
    GPR_ASSERT(response->payload->body->len == 314159);
    char buf[314159];
    bzero(buf, sizeof(buf));
    GPR_ASSERT(memcmp(response->payload->body->data, buf, 314159));

    /*
     * Create request with compression and check flags
     */
    request.response_compressed = 1;
    gpr_log(GPR_DEBUG, "Sending unary request with server compression=off");
    GPR_ASSERT(grpc__testing__test_service__unary_call__sync(client, md2, 0, 
							     &context, &request, 
							     -1) == GRPC_C_OK);
    GPR_ASSERT(context->gcc_stream_read(context, (void **)&response, 0, -1) 
	       == GRPC_C_OK);

    /*
     * Make sure data is compressed and payload is all zeros
     */
    GPR_ASSERT((grpc_call_test_only_get_message_flags(context->gcc_call) 
		& GRPC_WRITE_INTERNAL_COMPRESS));
    GPR_ASSERT(response->payload->body->len == 314159);
    GPR_ASSERT(memcmp(response->payload->body->data, buf, 314159));

    status = context->gcc_stream->finish(context, NULL, 0);

    /*
     * TODO: free
     */

    return 0;
}

/*
 * This test verifies that client-only streaming succeeds
 */
int 
client_streaming_fn (grpc_c_client_t *client)
{
    Grpc__Testing__StreamingInputCallRequest request;
    Grpc__Testing__StreamingInputCallResponse *response;
    grpc_c_context_t *context;
    grpc_c_status_t status;
    int i, aggregated_payload_size = 0;

    grpc__testing__StreamingInputCallRequest__init(&request);

    gpr_log(GPR_DEBUG, "Sending request steaming rpc");

    GPR_ASSERT(grpc__testing__test_service__streaming_input_call__sync(client, 
			    NULL, 0, &context, NULL, -1) == GRPC_C_OK);

    /*
     * Send payload in chunks
     */
    grpc__testing__payload__init(request.payload);
    for (i = 0; i < sizeof(request_stream_sizes); i++) {
	request.payload.body.data = malloc(request_stream_sizes[i]);
	bzero(request.payload.body.data, request_stream_sizes[i]);
	request.payload.body.len = request_stream_sizes[i];
	request.payload.has_body = 1;

	GPR_ASSERT(context->gcc_stream->write(context, &response, -1) 
		   == GRPC_C_OK);
	aggregated_payload_size += request_stream_sizes[i];
	free(request.payload.body.data);
    }

    /*
     * Finish the call and verify status
     */
    GPR_ASSERT(context->gcc_stream->finish(context, &status) == GRPC_C_OK);
    GPR_ASSERT(status.gcs_code == GRPC_C_OK);

    /*
     * Verify aggregated payload size
     */
    GPR_ASSERT(response->aggregated_payload_size == aggregated_payload_size);

    return 0;
}

int 
client_compressed_streaming_fn (grpc_c_client_t *client)
{
    Grpc__Testing__StreamingInputCallRequest request;
    Grpc__Testing__StreamingInputCallResponse *response;
    grpc_c_context_t *context;
    grpc_c_status_t status;
    int i, aggregated_payload_size = 0;
 
    grpc__testing__StreamingInputCallRequest__init(&request);

    gpr_log(GPR_DEBUG, "Sending probe for compressed streaming request.");

    GPR_ASSERT(grpc__testing__test_service__streaming_input_call__sync(client, 
			    NULL, 0, &context, NULL, -1) == GRPC_C_OK);

    return 0;
}

/*
 * Test map
 */
struct testdef {
    const char *testname;
    void (*testfn)(grpc_c_client_t *);
};

/*
 * Array of all our testcases and corresponding function that implements them
 */
static struct testdef tests[] = {
    {"empty_unary", empty_unary_fn},
    {"cacheable_unary", cacheable_unary_fn},
    {"large_unary", large_unary_fn},
    {"client_compressed_unary", client_compressed_unary_fn},
    {"server_compressed_unary", server_compressed_unary_fn},
    {"client_streaming", client_streaming_fn},
    {"client_compressed_streaming", client_compressed_streaming_fn},
    {0, 0}
};

/*
 * Find and run test function
 */
int 
run_test (grpc_c_client_t *client, const char *testname) {
    struct testdef *test = tests;

    while (test->testname != NULL) {
	if (streq(test->testname, testname)) {
	    break;
	}
	test++;
    }

    /*
     * Run if test is found
     */
    if (test->testname != NULL) {
	test->testfn(client);
    } else {
	printf("No testcases found with that name\n");
    }
}

int 
main (int argc, char **argv)
{
    int use_tls, use_test_ca;
    char *server_host, *server_port, *server_host_override, *test_case;
    char *default_service_account, *oauth_scope;
    char *service_account_key_file;
    char addr[1024];

    while (1) {
	int c, option_index = 0;
	static struct option long_options[] = {
	    {"server_host", required_argument, 0, 1},
	    {"server_host_override", required_argument, 0, 2}, 
	    {"test_case", required_argument, 0, 3}, 
	    {"default_service_account", required_argument, 0, 4},
	    {"use_tls", no_argument, 0, 5}, 
	    {"server_port", required_argument, 0, 6},
	    {"use_test_ca", no_argument, 0, 7},
	    {"oauth_scope", required_argument, 0, 8},
	    {"service_account_key_file", required_argument, 0, 9},
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
	    server_host = strdup(optarg);
	    break;
	case 2:
	    server_host_override = strdup(optarg);
	    break;
	case 3:
	    test_case = strdup(optarg);
	    break;
	case 4:
	    default_service_account = strdup(optarg);
	    break;
	case 5:
	    use_tls = 1;
	    break;
	case 6:
	    server_port = strdup(optarg);
	    break;
	case 7:
	    use_test_ca = 1;
	    break;
	case 8:
	    oauth_scope = strdup(optarg);
	    break;
	case 9:
	    service_account_key_file = strdup(optarg);
	    break;
	}
    }

    /*
     * Set up environment
     */
    grpc_c_init(GRPC_THREADS, NULL);

    snprintf(addr, sizeof(addr), "%s:%s", server_host, server_port);
    grpc_c_client_t *client = grpc_c_client_init_by_host(addr, test_case, 
							 NULL, NULL);

    run_test(client, test_case);
}
