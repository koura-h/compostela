/* $Id$ */
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>

#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>

#include <openssl/md5.h>

#include "supports.h"
#include "azlog.h"


char*
strdup_pathcat(const char* p0, const char* p1)
{
    char* p;
    size_t n0, n1, na;
    n0 = strlen(p0);
    n1 = strlen(p1);
    na = n0 + n1 + 1;
    if (p0[n0 - 1] != '/') {
        na++;
    }
    p = malloc(na);
    strcpy(p, p0); // n0(from p0) is shorter than na
    if (p0[n0 - 1] != '/') {
        p[n0] = '/';
    }
    strcat(p, p1);

    return p;
}

char*
pathcat(const char* p, ...)
{
    va_list ap;

    char buf[PATH_MAX];
    char *q, *s;

    size_t n = strlen(p);

    strncpy(buf, p, sizeof(buf) - 1);
    q = buf + n;
    va_start(ap, p);
    while (q < buf + sizeof(buf)) {
        s = va_arg(ap, char*);
	if (!s) {
	    break;
	}
	if (*(q - 1) != '/') {
	    *q++ = '/';
	    *q = '\0';
	}
	strncat(q, s, buf + sizeof(buf) - q - 1);
	q += strlen(s);
    }
    va_end(ap);

    return strdup(buf);
}


int
sendall(int s, const void* data, ssize_t len, int opt)
{
    const char *p0 = data, *p;
    ssize_t cb = 0, n = len;

    for (p = p0; n; ) {
        cb = send(s, p, n, opt);
        if (cb <= 0) {
            break;
        }

        //
        p += cb;
        n -= cb;
    }

    return (p - p0 ? p - p0 : cb);
}

int
recvall(int s, void* buf, ssize_t size, int opt)
{
    char *p0 = buf, *p;
    ssize_t cb = 0, n = size;

    for (p = p0; n; ) {
        cb = recv(s, p, n, opt);
	if (cb == -1 && errno == EAGAIN) {
	    usleep(100000);
	    continue;
	}
        if (cb <= 0) {
            break;
        }

        p += cb;
        n -= cb;
    }

    return (p - p0 ? p - p0 : cb);
}

int
set_non_blocking(int s)
{
    int flag = fcntl(s, F_GETFL, 0);
    if (fcntl(s, F_SETFL, flag | O_NONBLOCK) != 0) {
        perror("fcntl");
        return -1;
    }
    return 0;
}

//////

int
mhash_with_size(const char* fpath, off_t fsize, unsigned char** mhash, size_t* mhash_size)
{
    MD5_CTX ctx;
    unsigned char buf[2048];
    off_t na = fsize;
    int fd;

    fd = open(fpath, O_RDONLY);
    if (fd == -1) {
        *mhash = NULL;
        *mhash_size = 0;
        return -1;
    }

    MD5_Init(&ctx);
    *mhash_size = MD5_DIGEST_LENGTH;
    *mhash = malloc(*mhash_size);
    while (na > 0) {
        int n, d;

        d = (na < sizeof(buf) ? na : sizeof(buf));
        n = read(fd, buf, d);
        if (n <= 0) {
            break;
        }

        MD5_Update(&ctx, buf, n);
        na -= n;
    }

    MD5_Final(*mhash, &ctx);

    close(fd);
    return 0;
}

int
dump_mhash(const unsigned char* mhash, size_t mhash_size)
{
    int i;
    fprintf(stdout, "mhash:");
    for (i = 0; i < mhash_size; i++) {
        fprintf(stdout, " %02x", mhash[i]);
    }
    fprintf(stdout, "\n");
    return 0;
}

////

void
handler_sigpipe(int sig, siginfo_t* sinfo, void* ptr)
{
    az_log(LOG_DEBUG, ">>> SIGPIPE raised!");
}

int
set_sigpipe_handler()
{
    struct sigaction sa = {
        .sa_sigaction = handler_sigpipe,
        .sa_flags = SA_RESTART | SA_SIGINFO,
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, NULL);

    return 0;
}

////
size_t
__w3cdatetime(char* buf, size_t sz, time_t t)
{
    struct tm tm;
    char *p;
    size_t n;

    localtime_r(&t, &tm);

    n = strftime(buf, sz - 1, "%FT%T%z", &tm);
    p = buf + n;
    if (*p == '\0' && isdigit(*(p - 1)) && isdigit(*(p - 2))) {
        *(p + 1) = *p;       // == '\0'
        *(p    ) = *(p - 1);
        *(p - 1) = *(p - 2);
        *(p - 2)= ':';
    }

    return n + 1;
}
