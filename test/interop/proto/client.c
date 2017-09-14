/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include <unistd.h>

int 
main (int argc, char **argv)
{
    int server_port, use_tls, use_test_ca;
    char *server_host, server_host_override, test_case;
    char *default_service_account, --oauth_scope;
    char *service_account_key_file;

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
	    server_host_override = optarg;
	    break;
	case 3:
	    test_case = optarg;
	    break;
	case 4:
	    default_service_account = optarg;
	    break;
	case 5:
	    use_tls = 1;
	    break;
	case 6:
	    server_port = atoi(optarg);
	    break;
	case 7:
	    use_test_ca = 1;
	    break;
	case 8:
	    oauth_scope = optarg;
	    break;
	case 9:
	    service_account_key_file = optarg;
	    break;
	}
    }
}
