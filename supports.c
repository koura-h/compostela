/* $Id$ */
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>

#include "supports.h"


int
sendall(int s, const void* data, ssize_t len, int opt)
{
    const char* p = (const char*)data;
    ssize_t cb = 0, n = len;

    while (n) {
        cb = send(s, p, n, opt);
        if (cb == -1) {
            return -1;
        }

        //
        p += cb;
        n -= cb;
    }

    return len;
}

int
recvall(int s, void* buf, ssize_t size, int opt)
{
    char *p0 = buf, *p;
    ssize_t cb = 0, n = size;

    for (p = p0; n; ) {
        cb = recv(s, p, n, opt);
        if (cb <= 0) {
            break;
        }

        p += cb;
        n -= cb;
    }

    return (p - p0 ? p - p0 : cb);
}
