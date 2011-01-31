/* $Id$ */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>

#include <byteswap.h>

#include "scmessage.h"
#include "supports.h"


enum { PORT = 8187 };

typedef struct _sc_follow_context {
    char *filename;
    int channel;
    off_t current_position;
    off_t filesize;
    int _fd;
} sc_follow_context;


////////////////////

typedef struct _sc_aggregator_connection {
    int socket;
    char *buffer;
    //
    char *host;
    int port;
} sc_aggregator_connection;

sc_aggregator_connection*
sc_aggregator_connection_new(ssize_t bufsize, const char* host, int port)
{
    sc_aggregator_connection* conn = (sc_aggregator_connection*)malloc(sizeof(sc_aggregator_connection));
    if (conn) {
        conn->buffer = malloc(bufsize);
	conn->host = strdup(host);
	conn->port = port;
    }
    return conn;
}

int
sc_aggregator_connection_open(sc_aggregator_connection* conn)
{
    struct addrinfo hints, *ai, *ai0 = NULL;
    int err, s = -1;
    char sport[16];

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = PF_UNSPEC;
    hints.ai_flags = AI_NUMERICSERV;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(sport, sizeof(sport), "%d", conn->port);

    err = getaddrinfo(conn->host, sport, &hints, &ai0);
    if (err) {
        fprintf(stderr, "getaddrinfo: %s", gai_strerror(err));
        return -1;
    }

    for (ai = ai0; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (s < 0) {
	    continue;
	}

	if (connect(s, ai->ai_addr, ai->ai_addrlen) < 0) {
	    close(s);
	    continue;
	}
	conn->socket = s;
	break;
    }
    freeaddrinfo(ai0);

    return conn->socket != -1 ? 0 : -1;
}

int
sc_follow_context_sync_file(sc_follow_context *cxt, sc_aggregator_connection* conn)
{
    sc_message *msg, *resp;
    size_t n = strlen(cxt->filename);
    int64_t stlen = 0;
    int32_t len = 0;

    fprintf(stderr, ">>> INIT: started\n");

    msg = sc_message_new(n);
    if (!msg) {
        return -1;
    }

    msg->code    = htons(SCM_MSG_INIT);
    msg->channel = htons(0);
    msg->length  = htonl(n);
    memcpy(msg->content, cxt->filename, n);

    // send_message
    if (sc_aggregator_connection_send_message(conn, msg) != 0) {
        return -1;
    }

    if (sc_aggregator_connection_receive_message(conn, &resp) != 0) {
        return -3;
    }

    if (ntohs(resp->code) != SCM_RESP_OK) {
        fprintf(stderr, ">>> INIT: failed\n");
        return -4;
    }
    cxt->channel = htons(resp->channel);
    len = htonl(resp->length);
    stlen = *(int64_t*)(&resp->content);
#if __BYTE_ORDER == __LITTLE_ENDIAN
    stlen = bswap_64(stlen);
#endif
    fprintf(stderr, ">>> INIT: len = %d\n", len);
    if (len > sizeof(int64_t)) {
        unsigned char* buf, *p;
	size_t bufsize, psize;

	p = resp->content + sizeof(int64_t);
	psize = len - sizeof(int64_t);

	mhash_with_size(cxt->filename, stlen, &buf, &bufsize);
	if (buf) {
	    if (psize != bufsize || memcmp(p, buf, bufsize) != 0) {
	        fprintf(stderr, "mhash invalid!!!\n");
		exit(-1);
	    } else {
	        fprintf(stderr, "mhash check: OK\n");
	    }
	    free(buf);
	}
    }
    fprintf(stderr, "channel id = %d\n", cxt->channel);
    fprintf(stderr, "stlen = %d\n", stlen);
    lseek(cxt->_fd, stlen, SEEK_SET);

    fprintf(stderr, ">>> INIT: finished\n");
    return 0;
}


int
sc_aggregator_connection_close(sc_aggregator_connection* conn)
{
    close(conn->socket);
    conn->socket = -1;
    return 0;
}

int
sc_aggregator_connection_send_message(sc_aggregator_connection* conn, sc_message* msg)
{
    int ret = 0;
    int32_t len = ntohl(msg->length);

    fprintf(stderr, "buf->length = %lx\n", len);
    if ((ret = sendall(conn->socket, msg, len + offsetof(sc_message, content), 0)) <= 0) {
        fprintf(stderr, "sending error\n");
        return -1;
    }

    return 0;
}

int
sc_aggregator_connection_receive_message(sc_aggregator_connection* conn, sc_message** pmsg)
{
    char buf[offsetof(sc_message, content)];
    sc_message* m = (sc_message*)buf;
    int32_t len = 0;

    if (recvall(conn->socket, buf, sizeof(buf), 0) <= 0) {
        return -1;
    }

    len = ntohl(m->length);

    m = sc_message_new(len);
    if (!m) {
        return -2;
    }

    memcpy(m, buf, sizeof(buf));
    if (recvall(conn->socket, m->content, len, 0) <= 0) {
        return -3;
    }

    *pmsg = m;

    return 0;
}

void
sc_aggregator_connection_destroy(sc_aggregator_connection* conn)
{
    sc_aggregator_connection_close(conn);

    free(conn->host);
    free(conn->buffer);
    free(conn);
}

////////////////////

sc_follow_context*
sc_follow_context_new(const char* fname)
{
    sc_follow_context* cxt = NULL;

    cxt = (sc_follow_context*)malloc(sizeof(sc_follow_context));
    if (cxt) {
        memset(cxt, 0, sizeof(sc_follow_context));

        cxt->filename = strdup(fname);
	if (!cxt->filename) {
	    free(cxt);
	    cxt = NULL;
	}

        // we should read control files for 'fname'
    }

    return cxt;
}

int
sc_follow_context_open(sc_follow_context* cxt)
{
    // struct stat st;

    cxt->_fd = open(cxt->filename, O_RDONLY);
    if (cxt->_fd <= 0) {
        return -1;
    }

/*
    fstat(cxt->_fd, &st);
    cxt->filesize = st.st_size;
    lseek(cxt->_fd, cxt->current_position, SEEK_SET);
    */
    return 0;
}

int
_sc_follow_context_proc_data(sc_follow_context* cxt, sc_aggregator_connection* conn, sc_message* msg, sc_message** ppresp)
{
    int ret;
    *ppresp = NULL;

    if ((ret = sc_aggregator_connection_send_message(conn, msg)) != 0) {
	// connection broken
	fprintf(stderr, "connection has broken.\n");
	return ret;
    }

    if ((ret = sc_aggregator_connection_receive_message(conn, ppresp)) != 0) {
	fprintf(stderr, "connection has broken. (2) = %d\n", ret);

	// reconnect
	return ret;
    }

    return ret;
}

int
sc_follow_context_run(sc_follow_context* cxt, sc_aggregator_connection* conn)
{
    ssize_t csize = 2048;
    sc_message* msg = sc_message_new(csize), *resp = NULL;
    int ret = 0;

    fprintf(stderr, "context run\n");

    while (1) {
	int32_t cb = read(cxt->_fd, &msg->content, csize);
	if (cb <= 0) {
            sleep(1);
	    continue;
	}

	if (cxt->channel == 0) {
	    // to be registered
	}
	fprintf(stderr, "reading file...\n");

        msg->code    = htons(SCM_MSG_DATA);
	msg->channel = htons(cxt->channel);
	msg->length  = htonl(cb);

	if (_sc_follow_context_proc_data(cxt, conn, msg, &resp) != 0) {
	    // reconnect
	    fprintf(stderr, "reconnect now\n");
	    sc_aggregator_connection_open(conn);
            sc_follow_context_sync_file(cxt, conn);
	}

	// here, we proceed response from aggregator
	sc_message_destroy(resp);
    }
    sc_message_destroy(msg);
}

void
sc_follow_context_destroy(sc_follow_context* cxt)
{
    free(cxt->filename);
    free(cxt);
}

int
main(int argc, char** argv)
{
    sc_follow_context* cxt = NULL;
    sc_aggregator_connection* conn = NULL;

    const char* fname = (argc > 1 ? argv[1] : "default");
    const char* servhost = (argc > 2 ? argv[2] : "log");

    cxt = sc_follow_context_new(fname);
    sc_follow_context_open(cxt);

    fprintf(stderr, "cxt = %p\n", cxt);

    conn = sc_aggregator_connection_new(2048, servhost, PORT);
    sc_aggregator_connection_open(conn);
    fprintf(stderr, "conn = %p\n", conn);

    sc_follow_context_sync_file(cxt, conn);

    return sc_follow_context_run(cxt, conn);
}
