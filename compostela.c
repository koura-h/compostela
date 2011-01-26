/* $Id$ */
#include <sys/epoll.h>
#include <dirent.h>
#include <stdio.h>
#include <stddef.h>
#include <netdb.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>

#include <byteswap.h>

#include "scmessage.h"
#include "supports.h"

enum { PORT = 8187 };

enum { MAX_SOCKETS = 10 };
enum { MAX_EVENTS = 10 };

enum { BUFSIZE = 2048 };


////////////////////////////////////////

typedef struct _sc_channel {
    char *filename;
    char *remote_host;
} sc_channel;

sc_channel*
sc_channel_new(const char* fname, const char* addr)
{
    sc_channel* channel = (sc_channel*)malloc(sizeof(sc_channel));
    if (channel) {
        if (fname) {
	    channel->filename = strdup(fname);
	}
	if (addr) {
	    channel->remote_host = strdup(addr);
	}
    }
    return channel;
}

void
sc_channel_destroy(sc_channel* channel)
{
    free(channel->filename);
    free(channel->remote_host);
    free(channel);
}

////////////////////////////////////////

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
do_init(int c, sc_channel *ch)
{
    ssize_t n = 0;
    sc_message* buf = sc_message_new(BUFSIZE);
    sc_message* ok = sc_message_new(sizeof(int32_t));

    n = recvall(c, buf, offsetof(sc_message, content), 0);
    if (n > 0) {
        buf->length = ntohl(buf->length);
        n = recvall(c, &buf->content, buf->length, 0);
        // fprintf(stderr, "n = %d / buf->length = %d\n", n, buf->length);

	ch->filename = malloc(buf->length + 1);
	memcpy(ch->filename, buf->content, buf->length);
	ch->filename[buf->length] = '\0';

        // haha
	ok->code    = htons(SCM_RESP_OK);
	ok->channel = buf->channel;
	ok->length  = htonl(sizeof(int32_t));
	memset(ok->content, 0, sizeof(int32_t));
	// haha
        n = sendall(c, ok, offsetof(sc_message, content) + sizeof(int32_t), 0);
        _dump(buf);
    } else {
        close(c);
    }

    sc_message_destroy(ok);
    sc_message_destroy(buf);
}

int
_existsdir(const char* path)
{
    int ret = 0;

    DIR* dir = opendir(path);
    if (dir) {
        ret = 1;
	closedir(dir);
    }

    return ret;
}

int
_do_append_file(const char *data, size_t len, sc_channel* channel)
{
    int fd;
    char path[PATH_MAX];

    if (!_existsdir(channel->remote_host)) {
        mkdir(channel->remote_host, 0755);
    }

    snprintf(path, sizeof(path), "%s/%s", channel->remote_host, channel->filename);
    fprintf(stderr, "appending to path [%s]\n", path);

    fd = open(path, O_APPEND | O_RDWR | O_CREAT);
    if (fd > 0) {
        write(fd, data, len);
        close(fd);
    }

    return 0;
}

int
do_receive(int c, sc_channel* channel)
{
    ssize_t csize = 2048, n;
    sc_message* buf = sc_message_new(csize);
    sc_message* ok = sc_message_new(sizeof(int32_t));

    n = recvall(c, buf, offsetof(sc_message, content), 0);
    if (n > 0) {
        buf->length = ntohl(buf->length);
        // fprintf(stderr, "n = %d / buf->length = %d\n", n, buf->length);
        n = recvall(c, &buf->content, buf->length, 0);
        // fprintf(stderr, "n = %d / buf->length = %d\n", n, buf->length);

	_do_append_file(buf->content, buf->length, channel);

        // haha
	ok->code    = htons(SCM_RESP_OK);
	ok->channel = buf->channel;
	ok->length  = htonl(sizeof(int32_t));
	memset(ok->content, 0, sizeof(int32_t));
	// haha
        n = sendall(c, ok, offsetof(sc_message, content) + sizeof(int32_t), 0);
        // _dump(buf);
    } else {
        fprintf(stderr, "recvall error.\n");
        close(c);
    }

    sc_message_destroy(ok);
    sc_message_destroy(buf);
}

int
run_main(int* socks, int num_socks)
{
    struct epoll_event ev, events[MAX_EVENTS];
    int epfd, i, j;

    char buf[2048];
    ssize_t n;
    sc_channel* sc = NULL;

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
		    struct sockaddr_storage ss;
		    socklen_t sslen = sizeof(ss);
		    // j = num_socks;
		    char hbuf[NI_MAXHOST];
		    int err;

                    memset(&ss, 0, sizeof(ss));
		    c = accept(socks[j], (struct sockaddr*)&ss, &sslen);
		    if (c < 0) {
		        continue;
		    }

		    if ((err = getnameinfo((struct sockaddr*)&ss, sslen, hbuf, sizeof(hbuf), NULL, 0, NI_NUMERICHOST))) {
		        fprintf(stderr, "gai_strerror = [%s]\n", gai_strerror(err));
		    }
		    fprintf(stderr, "accept connection from [%s]\n", hbuf);
		    if (!sc) {
		        sc = sc_channel_new(NULL, hbuf);
		    }
		    fprintf(stderr, "sc = %p\n", sc);
		    fprintf(stderr, "sc->filename = %s\n", sc->filename);
		    fprintf(stderr, "sc->remote_host = %s\n", sc->remote_host);

		    do_init(c, sc);

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
	        do_receive(c, sc);
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

    char buf[2048], sport[NI_MAXSERV];
    ssize_t cb, n;

    int yes = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    snprintf(sport, sizeof(sport), "%d", PORT);
    err = getaddrinfo(NULL, sport, &hints, &res0);
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
