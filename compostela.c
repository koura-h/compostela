/* $Id$ */
#include <sys/epoll.h>
#include <stdio.h>
#include <stddef.h>
#include <netdb.h>
#include <string.h>
#include <fcntl.h>

#include "scmessage.h"

enum { MAX_SOCKETS = 10 };
enum { MAX_EVENTS = 10 };

void
_dump(sc_message* msg)
{
    fwrite(&msg->content, msg->length, 1, stdout);
    fflush(stdout);
}

int
set_non_blocking(int s)
{
    int flag = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flag | O_NONBLOCK);
    return 0;
}

int
do_receive(int c)
{
    ssize_t csize = 2048, n;
    sc_message* buf = sc_message_new(csize);
    n = recv(c, buf, offsetof(sc_message, content), 0); // haha
    fprintf(stderr, "n = %d / buf->length = %d\n", n, buf->length);

    n = recv(c, &buf->content, buf->length, 0);

    fprintf(stderr, "n = %d / buf->length = %d\n", n, buf->length);

    _dump(buf);

    sc_message_destroy(buf);
}

int
run_main(int* socks, int num_socks)
{
    struct epoll_event ev, events[MAX_EVENTS];
    int epfd, i, j;

    char buf[2048];
    ssize_t n;

    int done = 0;

    fprintf(stderr, "num_socks = %d\n", num_socks);

    if ((epfd = epoll_create(MAX_EVENTS)) < 0) {
        fprintf(stderr, "epoll_create error\n");
        return -1;
    }

    for (i = 0; i < num_socks; i++) {
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        ev.data.fd = socks[i];
	epoll_ctl(epfd, EPOLL_CTL_ADD, socks[i], &ev);
    }

    for (;;) {
	int nfd, c = -1;

	nfd = epoll_wait(epfd, events, MAX_EVENTS, -1);
	for (i = 0; i < nfd; i++) {
	    done = 0;
	    for (j = 0; j < num_socks; j++) {
	        if (events[i].data.fd == socks[j]) {
		    // j = num_socks;

		    c = accept(socks[j], NULL, NULL);
		    if (c < 0) {
		        continue;
		    }

		    set_non_blocking(c);
		    ev.events = EPOLLIN | EPOLLET;
		    ev.data.fd = c;
		    if (epoll_ctl(epfd, EPOLL_CTL_ADD, c, &ev) < 0) {
		        fprintf(stderr, "epoll set insertion error: fd = %d\n", c);
			continue;
		    }
		    done = 1;
		    break;
		}
	    }

	    if (!done) {
		c = events[i].data.fd;
	        do_receive(c);
	    }
	}
    }
}

int
main(int argc, char** argv)
{
    struct addrinfo hints, *res, *res0 = NULL;
    int err;
    int s[MAX_SOCKETS], nsock, i, c;

    char buf[2048];
    ssize_t cb, n;

    int yes = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    err = getaddrinfo(NULL, "8187", &hints, &res0);
    if (err) {
        return -1;
    }

    for (res = res0, nsock = 0; res && nsock < MAX_SOCKETS; res = res->ai_next) {
        s[nsock] = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (s[nsock] < 0) {
	    //
	    continue;
	}

	setsockopt(s[nsock], SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        if (bind(s[nsock], res->ai_addr, res->ai_addrlen) < 0) {
	    close(s[nsock]);
	    continue;
	}

	listen(s[nsock], 5);
	nsock++;
    }
    freeaddrinfo(res0);

    run_main(s, nsock);

    for (i = 0; i < nsock; i++) {
        close(s[i]);
    }

    return 0;
}
