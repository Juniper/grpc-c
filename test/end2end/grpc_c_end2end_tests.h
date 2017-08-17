/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef GRPC_C_END2END_TESTS_H
#define GRPC_C_END2END_TESTS_H

#include "grpc-c/grpc-c.h"
#include "common/strextra.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Structure to hold testcase information
 */
typedef struct gc_end2end_test_s {
    const char *gct_name;
    grpc_c_client_t *(*gct_client_init)(const char *addr, const char *id);
    grpc_c_server_t *(*gct_server_init)(const char *addr);
    void (*gct_client_func)(grpc_c_client_t *client);
    void (*gct_server_func)(grpc_c_server_t *server);
    int (*gct_test_func)(void);	/* Function that returns 0 when all test 
				   conditions are met. Else returns 1 */
    void (*gct_client_teardown)(grpc_c_client_t *client);
    void (*gct_server_teardown)(grpc_c_server_t *server);
    int gct_nclients;	/* Maximum number of clients */
} gc_end2end_test_t;

#define GRPC_C_END2END_TEST_DECLARE(foo) \
    void foo ## _server(grpc_c_server_t *server); \
    void foo ## _client(grpc_c_client_t *client); \
    int foo ## _test(void); \

#if defined(GRPC_C_END2END_TEST_CLIENT)
#define GRPC_C_END2END_TEST(foo, nclients) \
    {#foo, gc_client_init, NULL, foo ## _client, NULL, foo ## _test, \
	gc_client_teardown, NULL, nclients}
#elif defined(GRPC_C_END2END_TEST_SERVER)
#define GRPC_C_END2END_TEST(foo, nclients) \
    {#foo, NULL, gc_server_init, NULL, foo ## _server, foo ## _test, \
	NULL, gc_server_teardown, nclients}
#endif

grpc_c_client_t *
gc_client_init (const char *addr, const char *id);

grpc_c_server_t *
gc_server_init (const char *addr);

void gc_client_teardown (grpc_c_client_t *client);

void gc_server_teardown (grpc_c_server_t *server);

/*
 * Use this to declare functions corresponding to each testcase
 */
GRPC_C_END2END_TEST_DECLARE(unary_sync);

/*
 * This array holds entries for all our tests
 */
gc_end2end_test_t gc_end2end_tests[] = {
    GRPC_C_END2END_TEST(unary_sync, 1), 
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0 }
};

#ifdef __cplusplus
}
#endif

#endif /* GRPC_C_END2END_TESTS_H */
