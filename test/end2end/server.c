/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include <signal.h>

#include "common/aux_types.h"

#define GRPC_C_END2END_TEST_SERVER
#include "grpc_c_end2end_tests.h"

static grpc_c_server_t *test_server;
static gc_end2end_test_t test;

static void sigint_handler (int x UNUSED) {
    grpc_c_server_destroy(test_server);
    grpc_c_shutdown();
    exit(0);
}

static void *
test_check (void *arg UNUSED)
{
    while (test.gct_test_func() != 0) {}

    grpc_c_server_destroy(test_server);
    grpc_c_shutdown();

    return NULL;
}

grpc_c_server_t *
gc_server_init (const char *addr)
{
    return grpc_c_server_create_by_host(addr, NULL, NULL);
}

void 
gc_server_teardown (grpc_c_server_t *server UNUSED)
{
}

/*
 * Takes two arguments, First is testcase name and second is address
 */
int 
main (int argc, char **argv)
{
    int i = 0;

    if (argc < 3) {
	fprintf(stderr, "Too few arguments\n");
	exit(1);
    }

    signal(SIGINT, sigint_handler);

    grpc_c_init(GRPC_THREADS, NULL);

    /*
     * Check for testcase and initialize corresponding stubs
     */
    while (gc_end2end_tests[i].gct_name != NULL) {
	if (streq(gc_end2end_tests[i].gct_name, argv[1])) {
	    test = gc_end2end_tests[i];
	    break;
	}
	i++;
    }

    if (test.gct_name == NULL) {
	fprintf(stderr, "Invalid testcase");
	exit(1);
    }

    /*
     * Perform the test
     */
    test_server = test.gct_server_init(argv[2]);

    test.gct_server_func(test_server);

    pthread_t thr;
    pthread_create(&thr, NULL, test_check, NULL);

    grpc_c_server_start(test_server);
    grpc_c_server_wait(test_server);

    test.gct_server_teardown(test_server);

    return 0;
}
