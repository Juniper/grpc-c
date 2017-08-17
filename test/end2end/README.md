# End2end tests
1. To run individual tests, do `make <testcase>_test` where <testcase> is name of the test you want to run
2. To run all the tests, do `make tests`
3. To add a new test case, 
..*add a proto file corresponding to the test
..*add corresponding entries in Makefile.am and grpc_c_end2end_tests.h
..*add <testcase>_client.c and Mtestcase>_server.c files where client and server implementations are available
..*Refer to unary_sync_server.c and unary_sync_client.c for more details
..*exit(1) or assert to fail a test
  
