/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/subprocess.h>

int
main (int argc, char **argv)
{
    gpr_subprocess *server, *client;
    char *me = argv[0];
    char *lslash = strrchr(me, '/');
    char root[BUFSIZ];
    char *args[3];
    int client_status, server_status;

    if (argc < 3) {
        fprintf(stderr, "Too few arguments\n");
        exit(1);
    }

    if (lslash) {
        memcpy(root, me, (size_t)(lslash - me));
        root[lslash - me] = 0;
    } else {
        strcpy(root, ".");
    }

    printf("-- %s --\n", argv[1]);
    gpr_asprintf(&args[0], "%s/server", root);
    args[1] = argv[1];
    args[2] = argv[2];

    server = gpr_subprocess_create(3, (const char **)args);
    gpr_free(args[0]);

    sleep(1);

    gpr_asprintf(&args[0], "%s/client", root);
    args[1] = argv[1];
    args[2] = argv[2];

    client = gpr_subprocess_create(3, (const char **)args);
    gpr_free(args[0]);

    if ((client_status = gpr_subprocess_join(client))) {
        gpr_subprocess_destroy(server);
        exit(1);
    }

    gpr_subprocess_interrupt(server);
    server_status = gpr_subprocess_join(server);
    (client_status || server_status) ? exit(1) : exit(0);
}
