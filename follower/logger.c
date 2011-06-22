/* $Id$ */
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

#include "supports.h"



int
writeall(int s, const void* data, ssize_t len)
{
    const char *p0 = data, *p;
    ssize_t cb = 0, n = len;

    for (p = p0; n; ) {
        cb = write(s, p, n);
        if (cb <= 0) {
            break;
        }

        //
        p += cb;
        n -= cb;
    }

    return (p - p0 ? p - p0 : cb);
}


#define RUNDIR	"/var/run/compostela/tmp"

void
usage()
{
    fprintf(stdout, "usage:\n");
}

char*
__setup_tempdir(const char* dir, const char* tag)
{
    char *fpath;

    fpath = malloc(strlen(dir) + strlen(tag) + 9);
    if (!fpath) {
        return NULL;
    }
    strcpy(fpath, dir);
    strcat(fpath, "/");
    strcat(fpath, tag);
    strcat(fpath, ".XXXXXX");

    return mkdtemp(fpath);
}

char*
__setup_tempfile(const char* dir)
{
    char *fname;
    size_t n = strlen(dir) + 18;
    time_t t;

    time(&t);
    fname = malloc(n);
    if (!fname) {
        return NULL;
    }

    snprintf(fname, n, "%s/%016lx", dir, t);
    return fname;
}


int
__make_local_socket(const char *path)
{
    int fd, ret;
    struct sockaddr_un sun;

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("comlogger: socket");
        return -1;
    }

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = PF_UNIX;
    strcpy(sun.sun_path, path);

    ret = connect(fd, (struct sockaddr*)&sun, sizeof(sun));
    if (ret == -1) {
        perror("comlogger: connect");
        // exit(-1);
        return -1;
    }

    return fd;
}

char*
__make_command(const char* tag, const char* path, int fdel)
{
    size_t n;
    char* p;

    n = strlen(tag) + strlen(path) + 11;
    p = malloc(n);
    if (!p) {
        perror("comlogger: malloc");
        return NULL;
    }

    snprintf(p, n, "OPEN -d %s %s\r\n", tag, path);
    return p;
}

int
__process(int fd, const char* tag, const char* ftemp, const char* data, size_t len)
{
    char *p;
    int f;
    ssize_t n;
    char buf[8];

    f = open(ftemp, O_WRONLY | O_CREAT, 0644);
    if (f == -1) {
        perror("comlogger: open");
        exit(-1);
    }
    n = writeall(f, data, len);
    if (n != len) {
        perror("comlogger: write");
        exit(-1);
    }
    close(f);

    p = __make_command(tag, ftemp, 1);
    if (!p) {
        perror("comlogger: __make_command");
        exit(-1);
    }
    sendall(fd, p, strlen(p), 0);
    free(p);
    
    n = recv(fd, buf, 4, MSG_WAITALL);
    if (n <= 0) {
        perror("comlogger: recv");
        exit(-1);
    }
    buf[n] = '\0';

    return strcmp(buf, "OK\r\n");
}

int
main(int argc, char** argv)
{
    int fd, ch;
    char* msg = NULL, *path = NULL, *tag = NULL, *dir = NULL, *ftemp = NULL;
    FILE* f = NULL;

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

    if (!path || !tag) {
        usage();
        exit(-1);
    }

    // from here

    dir = __setup_tempdir(RUNDIR, tag);
    if (!dir) {
        perror("comlogger: __setup_temp");
        exit(-1);
    }

    if (argc > 0) {
        int n = strlen(argv[0]);
        msg = malloc(n + 3);
        snprintf(msg, n + 3, "%s\r\n", argv[0]);
    }

    if (!msg) {
        f = stdin;
    }

    fd = __make_local_socket(path);
    if (fd == -1) {
        perror("comlogger: socket");
        exit(-1);
    }

    if (msg) {
        // send(fd, msg, strlen(msg), 0);
        ftemp = __setup_tempfile(dir);
        if (!ftemp) {
            perror("comlogger: __setup_tempfile");
            exit(-1);
        }

        __process(fd, tag, ftemp, msg, strlen(msg));
        free(ftemp);
    } else {
        char buf[8192];

	while (fgets(buf, sizeof(buf), stdin) != NULL) {
            // send(fd, buf, strlen(buf), 0);
            ftemp = __setup_tempfile(dir);
            if (!ftemp) {
                perror("comlogger: __setup_tempfile");
                exit(-1);
            }

            __process(fd, tag, ftemp, buf, strlen(buf));
            free(ftemp);
        }
    }

    free(dir);
    close(fd);
    return 0;
}


