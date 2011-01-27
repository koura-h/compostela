/* $Id$ */
#include <sys/epoll.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stddef.h>
#include <netdb.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <libgen.h>

#include <byteswap.h>

#include "scmessage.h"
#include "supports.h"

enum { PORT = 8187 };

enum { MAX_SOCKETS = 10 };
enum { MAX_EVENTS = 10 };

enum { BUFSIZE = 2048 };


int default_mode = 0644;


////////////////////////////////////////

typedef struct _sc_channel {
    int id;
    char *filename;
    //
    struct _sc_connection* connection;
    //
    struct _sc_channel *_next;
} sc_channel;

////////////////////////////////////////

typedef struct _sc_connection {
    struct sockaddr* sockaddr;
    socklen_t salen;
    //
    char *remote_addr;
    //
    // channel list here
    struct _sc_channel* channel_list;
    //
    int socket;
} sc_connection;

////////////////////////////////////////

sc_channel*
sc_channel_new(const char* fname, sc_connection* conn)
{
    sc_channel* channel = (sc_channel*)malloc(sizeof(sc_channel));
    if (channel) {
        if (fname) {
	    channel->filename = strdup(fname);
	}
	channel->connection = conn;
	channel->_next = NULL;
    }
    return channel;
}

void
sc_channel_destroy(sc_channel* channel)
{
    fprintf(stderr, "%s(%d) %s\n", __FILE__, __LINE__, __func__);
    free(channel->filename);
    free(channel);
}

////////////////////////////////////////

void
sc_connection_set_remote_addr(sc_connection* conn, struct sockaddr* sa, socklen_t salen);

sc_connection*
sc_connection_new(struct sockaddr* sa, socklen_t salen, int s)
{
    sc_connection *conn = (sc_connection*)malloc(sizeof(sc_connection));
    if (!conn) {
        return NULL;
    }

    conn->sockaddr = (struct sockaddr*)malloc(salen);
    if (conn->sockaddr) {
        memcpy(conn->sockaddr, sa, salen);
        conn->salen = salen;
	conn->socket = s;
	conn->channel_list = NULL;
	conn->remote_addr = NULL;

	sc_connection_set_remote_addr(conn, sa, salen);
    } else {
        free(conn);
	conn = NULL;
    }
    return conn;
}

void
sc_connection_set_remote_addr(sc_connection* conn, struct sockaddr* sa, socklen_t salen)
{
    if (!conn->remote_addr) {
	int err;
	char hbuf[NI_MAXHOST];
	if ((err = getnameinfo(sa, salen, hbuf, sizeof(hbuf), NULL, 0, NI_NUMERICHOST))) {
	    fprintf(stderr, "gai_strerror = [%s]\n", gai_strerror(err));
	    return;
        }
        fprintf(stderr, "accept connection from [%s]\n", hbuf);
        conn->remote_addr = strdup(hbuf);
    }
}

void
sc_connection_destroy(sc_connection* conn)
{
    sc_channel* c = conn->channel_list, *c0;

    fprintf(stderr, "%s(%d) %s\n", __FILE__, __LINE__, __func__);

    while (c) {
        c0 = c;
	c = c->_next;
        sc_channel_destroy(c0);
    }

    free(conn->remote_addr);
    free(conn->sockaddr);
    free(conn);
}

int
sc_connection_register_channel(sc_connection* conn, sc_channel* channel)
{
    int id = 0;
    sc_channel* last = NULL, *ch;

    for (ch = conn->channel_list; ch; ch = ch->_next) {
        if (channel == ch) {
	    // error: double registration
	    return -1;
	}
	id = (id < ch->id ? ch->id : id);
        //
        last = ch;
    }

    if (last) {
        last->_next = channel;
    } else {
        conn->channel_list = channel;
    }
    channel->_next = NULL;
    channel->id = id + 1;

    return 0;
}

sc_channel*
sc_connection_channel(sc_connection* conn, int id)
{
    sc_channel* channel = NULL;
    for (channel = conn->channel_list; channel; channel = channel->_next) {
        if (channel->id == id) {
	    break;
	}
    }
    return channel;
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
_create_dir(const char* fpath, mode_t mode)
{
    char *s = strdup(fpath), *p;
    if (!s) {
        return -1;
    }

    p = s;
    while (p = strchr(p, '/')) {
        *p = '\0';
	mkdir(s, mode);
	*p++ = '/';
    }

    mkdir(s, mode);
    free(s);
    return 0;
}


int
_do_append_file(const char *data, size_t len, const char* remote_addr, const char* fname)
{
    int fd;
    char path[PATH_MAX], dir[PATH_MAX];

    snprintf(path, sizeof(path), "%s/%s", remote_addr, fname);
    strcpy(dir, path);
    fprintf(stderr, "appending to ... [%s]\n", path);

    _create_dir(dirname(dir), 0777);

    fd = open(path, O_APPEND | O_RDWR | O_CREAT, default_mode);
    if (fd == -1) {
        perror("");
	return -1;
    }
    write(fd, data, len);
    close(fd);

    return 0;
}

int
_do_stat(const char *remote_addr, const char* fname, struct stat *pst)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", remote_addr, fname);

    return stat(path, pst);
}

int
handler_init(sc_message* msg, sc_connection* conn)
{
    int n;
    struct stat st;
    int64_t stlen = 0;

    sc_message* ok = sc_message_new(sizeof(int32_t));
    sc_channel* channel = sc_channel_new(NULL, conn);

    channel->filename = malloc(msg->length + 1);
    memcpy(channel->filename, msg->content, msg->length);
    channel->filename[msg->length] = '\0';

    memset(&st, 0, sizeof(st));
    _do_stat(conn->remote_addr, channel->filename, &st);

    fprintf(stderr, "channel->filename = %s\n", channel->filename);
    fprintf(stderr, "conn->remote_addr = %s\n", conn->remote_addr);

    sc_connection_register_channel(conn, channel);

    // haha
    ok->code    = htons(SCM_RESP_OK);
    ok->channel = htons(channel->id);
    ok->length  = htonl(sizeof(stlen));
    stlen = st.st_size;
#if __BYTE_ORDER == __LITTLE_ENDIAN
    stlen = bswap_64(stlen);
#endif
    memcpy(ok->content, &stlen, sizeof(stlen));
    // haha
    n = sendall(conn->socket, ok, offsetof(sc_message, content) + sizeof(stlen), 0);

    return 0;
}

int
handler_data(sc_message* msg, sc_connection* conn)
{
    int n;
    sc_channel* channel = sc_connection_channel(conn, msg->channel);

    fprintf(stderr, ">>> handler_data\n");

    sc_message* ok = sc_message_new(sizeof(int32_t));
    _do_append_file(msg->content, msg->length, conn->remote_addr, channel->filename);

    // haha
    ok->code    = htons(SCM_RESP_OK);
    ok->channel = htons(msg->channel);
    ok->length  = htonl(sizeof(int32_t));
    memset(ok->content, 0, sizeof(int32_t));
    // haha
    n = sendall(conn->socket, ok, offsetof(sc_message, content) + sizeof(int32_t), 0);
    // _dump(msg);
    sc_message_destroy(ok);

    return 0;
}

int
do_receive(int epfd, sc_connection* conn)
{
    ssize_t csize = 2048, n;
    int16_t code = 0;
    sc_message* msg = sc_message_new(csize);

    int c = conn->socket;

    n = recvall(c, msg, offsetof(sc_message, content), 0);
    if (n > 0) {
        msg->code    = ntohs(msg->code);
	msg->channel = ntohs(msg->channel);
        msg->length  = ntohl(msg->length);
        n = recvall(c, &msg->content, msg->length, 0);

	code = msg->code;
	if (msg->channel == 0) {
	    code = SCM_MSG_INIT;
	}

	switch (msg->code) {
	case SCM_MSG_INIT:
	    handler_init(msg, conn);
	    break;
	case SCM_MSG_DATA:
	    handler_data(msg, conn);
	    break;
	}
    } else if (n == 0) {
        struct epoll_event ev;

        fprintf(stderr, "connection closed\n");

	epoll_ctl(epfd, EPOLL_CTL_DEL, c, NULL);

	close(c);

	sc_connection_destroy(conn);
    } else {
        struct epoll_event ev;

        fprintf(stderr, "recvall error.\n");

	epoll_ctl(epfd, EPOLL_CTL_DEL, c, NULL);

        close(c);

	sc_connection_destroy(conn);
    }

    sc_message_destroy(msg);
    return 0;
}

int
run_main(int* socks, int num_socks)
{
    struct epoll_event ev, events[MAX_EVENTS];
    int epfd, i, j;

    char buf[2048];
    ssize_t n;
    sc_connection *conn = NULL;

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
		    int err;

                    memset(&ss, 0, sizeof(ss));
		    c = accept(socks[j], (struct sockaddr*)&ss, &sslen);
		    if (c < 0) {
		        continue;
		    }

		    conn = sc_connection_new((struct sockaddr*)&ss, sslen, c);

		    set_non_blocking(c);
		    ev.events = EPOLLIN | EPOLLET;
		    ev.data.ptr = conn;
		    if (epoll_ctl(epfd, EPOLL_CTL_ADD, c, &ev) < 0) {
		        fprintf(stderr, "epoll set insertion error: fd = %d\n", c);
			continue;
		    }
		    done = 1;
		    break;
		}
	    }

	    if (!done) {
		// c = events[i].data.fd;
		sc_connection* conn = events[i].data.ptr;
	        do_receive(epfd, conn);
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
