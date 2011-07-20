/* $Id$ */
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
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


#define OUTDIR "./"


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
__setup_tempfile(char* fname, size_t size, const char* dir, const char* pat)
{
    time_t t;
    struct tm tm;

    time(&t);
    if (!fname || size == 0) {
        return NULL;
    }

    localtime_r(&t, &tm);

    strftime(fname, size, pat, &tm);

    // snprintf(fname, n, "%s/%016lx", dir, t);
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

    f = open(ftemp, O_WRONLY | O_APPEND | O_CREAT, 0644);
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

    if (fd != -1) {
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
    } else {
        return 0;
    }
}

int
main(int argc, char** argv)
{
    int fd = -1, ch;
    char* msg = NULL, *tag = NULL, *path = NULL, *dateformat = NULL, *outdir = NULL;
    FILE* f = NULL;
    char ftemp[PATH_MAX];

    struct option long_opts[] = {
        { "dateformat", 2, NULL, 0 },
        { "outdir", 2, NULL, 0 },
        { "unix-socket", 2, NULL, 0 },
        { "help", 2, NULL, 0 },
    };

    while ((ch = getopt_long(argc, argv, "d:o:t:u:h", long_opts, NULL)) != -1) {
        switch (ch) {
        case 'd':
            dateformat = strdup(optarg);
            break;
        case 'o':
            outdir = strdup(optarg);
            break;
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

    if (!outdir) {
        outdir = strdup(OUTDIR);
    }

    // if (!path || !dateformat) {
    if (!dateformat) {
        usage();
        exit(-1);
    }

    // from here

    if (argc > 0) {
        int n = strlen(argv[0]);
        msg = malloc(n + 3);
        snprintf(msg, n + 3, "%s\r\n", argv[0]);
    }

    if (!msg) {
        f = stdin;
    }

    if (path) {
        fd = __make_local_socket(path);
        if (fd == -1) {
            perror("comlogger: socket");
            exit(-1);
        }
    }

    if (msg) {
        // send(fd, msg, strlen(msg), 0);
        __setup_tempfile(ftemp, sizeof(ftemp), outdir, dateformat);
        if (!ftemp || ftemp[0] == '\0') {
            perror("comlogger: __setup_tempfile");
            exit(-1);
        }

        __process(fd, tag, ftemp, msg, strlen(msg));
    } else {
        char buf[2048];

	while (fgets(buf, sizeof(buf), stdin) != NULL) {
            // send(fd, buf, strlen(buf), 0);
            __setup_tempfile(ftemp, sizeof(ftemp), outdir, dateformat);
            if (!ftemp || ftemp[0] == '\0') {
                perror("comlogger: __setup_tempfile");
                exit(-1);
            }
            fprintf(stderr, "buf = [%s]", buf);

            __process(fd, tag, ftemp, buf, strlen(buf));
        }
    }

    if (outdir)     { free(outdir); }
    if (dateformat) { free(dateformat); }
    if (tag)        { free(tag); }

    close(fd);
    return 0;
}
