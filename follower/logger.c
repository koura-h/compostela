#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>

#include "supports.h"


void
usage()
{
    fprintf(stdout, "usage:\n");
}

int
main(int argc, char** argv)
{
    int fd, ret, ch, n;
    struct sockaddr_un sun;
    char* msg = NULL, *path = NULL, *tag = NULL;
    FILE* f = NULL;
    char buf[2048], *p;

    struct option long_opts[] = {
        { "tag", 2, NULL, 0 },
        { "unix-socket", 2, NULL, 0 },
        { "help", 2, NULL, 0 },
    };

    while ((ch = getopt_long(argc, argv, "t:u:h", long_opts, NULL)) != -1) {
        switch (ch) {
        case 't':
            tag = strdup(optarg);
            break;
        case 'u':
            path = strdup(optarg);
            break;
        case 'h':
        default:
            usage();
            exit(1);
        }
    }
    argc -= optind;
    argv += optind;

    if (argc > 0) {
        int n = strlen(argv[0]);
        msg = malloc(n + 3);
        snprintf(msg, n + 3, "%s\r\n", argv[0]);
    }

    if (!path || !tag) {
        return -1;
    }

    if (!msg) {
        f = stdin;
    }

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("comlogger: socket");
        exit(-1);
    }

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = PF_UNIX;
    strcpy(sun.sun_path, path);

    ret = connect(fd, (struct sockaddr*)&sun, sizeof(sun));
    if (ret == -1) {
        perror("comlogger: connect");
        // exit(-1);
    }

    n = strlen(path) + 8;
    p = malloc(n);
    if (!p) {
        perror("comlogger: malloc");
        exit(-1);
    }
    snprintf(p, n, "OPEN %s\r\n", tag);
    sendall(fd, p, strlen(p), 0);
    free(p);
    
    n = recv(fd, buf, 4, MSG_WAITALL);
    if (n <= 0) {
        perror("comlogger: recv");
        exit(-1);
    }

    if (msg) {
        send(fd, msg, strlen(msg), 0);
    } else {
	while (fgets(buf, sizeof(buf), stdin) != NULL) {
	    // syslog(pri, "%s", buf);
            send(fd, buf, strlen(buf), 0);
        }
    }

    close(fd);
    return 0;
}
