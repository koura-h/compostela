/* $Id$ */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <assert.h>
#include <getopt.h>
#include <libgen.h>

#include <byteswap.h>

#include "azlist.h"
#include "azbuffer.h"

#include "scmessage.h"
#include "supports.h"

enum { BUFSIZE = 8196 };

char* g_config_server_addr = NULL;
int g_config_server_port = 8187;

typedef struct _sc_follow_context {
    char *filename;
    int channel;
    off_t current_position;
    off_t filesize;
    mode_t mode;
    int _fd;
    //
    az_buffer* buffer;
    //
    char *displayname;
    //
    struct _sc_aggregator_connection *connection;
} sc_follow_context;


////////////////////

typedef struct _sc_aggregator_connection {
    int socket;
    //
    char *host;
    int port;
} sc_aggregator_connection;

sc_aggregator_connection*
sc_aggregator_connection_new(const char* host, int port)
{
    sc_aggregator_connection* conn = (sc_aggregator_connection*)malloc(sizeof(sc_aggregator_connection));
    if (conn) {
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
sc_follow_context_sync_file(sc_follow_context *cxt)
{
    sc_message *msg, *resp;
    size_t n = strlen(cxt->displayname);
    int64_t stlen = 0;
    int32_t attr = 0, len = 0;

    fprintf(stderr, ">>> INIT: started\n");

    msg = sc_message_new(n + sizeof(int32_t));
    if (!msg) {
        return -1;
    }

    if (S_ISREG(cxt->mode)) {
        attr = 0x80000000;
    }

    msg->code    = htons(SCM_MSG_INIT);
    msg->channel = htons(0);
    msg->length  = htonl(n + sizeof(int32_t));
    *(int32_t*)(&msg->content) = htonl(attr);
    memcpy(msg->content + sizeof(int32_t), cxt->displayname, n);

    // send_message
    if (sc_aggregator_connection_send_message(cxt->connection, msg) != 0) {
        return -1;
    }

    if (sc_aggregator_connection_receive_message(cxt->connection, &resp) != 0) {
        return -3;
    }

    if (ntohs(resp->code) != SCM_RESP_OK) {
        fprintf(stderr, ">>> INIT: failed (code=%d)\n", ntohs(resp->code));
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
	} else {
	    fprintf(stderr, "mhash not found\n");
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

    fprintf(stderr, "send_message: code = %d, channel = %d, length = %lx\n", ntohs(msg->code), ntohs(msg->channel), len);
    if ((ret = sendall(conn->socket, msg, len + offsetof(sc_message, content), 0)) <= 0) {
        perror("sendall");
        fprintf(stderr, "sending error\n");
        return -1;
    }
    fprintf(stderr, "send_message: done\n");

    return 0;
}

int
sc_aggregator_connection_receive_message(sc_aggregator_connection* conn, sc_message** pmsg)
{
    char buf[offsetof(sc_message, content)];
    sc_message* m = (sc_message*)buf;
    int32_t len = 0;
    int n;

    n = recvall(conn->socket, buf, sizeof(buf), 0);
    if (n < 0) {
        perror("recvall");
        return -1;
    } else if (n == 0) {
        fprintf(stderr, "closed\n");
	return -4;
    }

    len = ntohl(m->length);

    m = sc_message_new(len);
    if (!m) {
        return -2;
    }

    memcpy(m, buf, sizeof(buf));
    if (len > 0) {
        if (recvall(conn->socket, m->content, len, 0) <= 0) {
            return -3;
        }
    }

    *pmsg = m;

    return 0;
}

void
sc_aggregator_connection_destroy(sc_aggregator_connection* conn)
{
    sc_aggregator_connection_close(conn);

    free(conn->host);
    free(conn);
}

////////////////////

sc_follow_context*
sc_follow_context_new(const char* fname, const char* dispname, sc_aggregator_connection* conn)
{
    sc_follow_context* cxt = NULL;

    cxt = (sc_follow_context*)malloc(sizeof(sc_follow_context));
    if (cxt) {
        memset(cxt, 0, sizeof(sc_follow_context));

	cxt->connection = conn;
	cxt->_fd = -1;

        cxt->filename = strdup(fname);
	if (!cxt->filename) {
	    free(cxt);
	    cxt = NULL;
	}

        cxt->displayname = strdup((dispname ? dispname : fname));
        if (!cxt->displayname) {
            free(cxt->filename);
            free(cxt);
            cxt = NULL;
        }
        // we should read control files for 'fname'

        cxt->buffer = az_buffer_new(BUFSIZE);
    }

    return cxt;
}

int
sc_follow_context_open_file(sc_follow_context* cxt, int use_lseek)
{
    struct stat st;
    int flags;

    cxt->_fd = open(cxt->filename, O_RDONLY);
    if (cxt->_fd < 0) {
        return -1;
    }

    fstat(cxt->_fd, &st);
    cxt->filesize = st.st_size;
    cxt->mode = st.st_mode;

    if (S_ISFIFO(cxt->mode)) {
        flags = fcntl(cxt->_fd, F_GETFL);
        fcntl(cxt->_fd, F_SETFL, flags | O_NONBLOCK);
    }

    if (use_lseek) {
        lseek(cxt->_fd, cxt->current_position, SEEK_SET);
    }
    return 0;
}

int
sc_follow_context_close_file(sc_follow_context* cxt)
{
    assert(cxt != NULL);
    if (cxt->_fd > 0) {
        close(cxt->_fd);
	cxt->_fd = -1;
    }
    return 0;
}

int
_sc_follow_context_proc_data(sc_follow_context* cxt, sc_message* msg, sc_message** ppresp)
{
    int ret;
    *ppresp = NULL;

    if ((ret = sc_aggregator_connection_send_message(cxt->connection, msg)) != 0) {
	// connection broken
	fprintf(stderr, "connection has broken.\n");
	return ret;
    }

    if ((ret = sc_aggregator_connection_receive_message(cxt->connection, ppresp)) != 0) {
	fprintf(stderr, "connection has broken. (2) = %d\n", ret);

	// reconnect
	return ret;
    }

    return ret;
}

int
_sc_follow_context_proc_dele(sc_follow_context* cxt, sc_message* msg, sc_message** ppresp)
{
    int ret = 0;
    *ppresp = NULL;

    if ((ret = sc_aggregator_connection_send_message(cxt->connection, msg)) != 0) {
	// connection broken
	fprintf(stderr, "connection has broken.\n");
	return ret;
    }

    if ((ret = sc_aggregator_connection_receive_message(cxt->connection, ppresp)) != 0) {
	fprintf(stderr, "connection has broken. (2) = %d\n", ret);

	// reconnect
	return ret;
    }

    return ret;
}

int
_sc_follow_context_read_line(sc_follow_context* cxt, char* dst, size_t dsize)
{
    int n, m;
    char* p = dst;
    size_t u;

    fprintf(stderr, ">>> _sc_follow_context_read_line\n");

    if (az_buffer_unread_bytes(cxt->buffer) == 0) {
        cxt->buffer->cursor = cxt->buffer->buffer;
	cxt->buffer->used = 0;
        n = az_buffer_fetch_file(cxt->buffer, cxt->_fd, az_buffer_unused_bytes(cxt->buffer));
	if (n <= 0) {
	    return n;
	}
    }

    while ((n = az_buffer_read_line(cxt->buffer, p, dst + dsize - p, &u)) != 0) {
        assert(n > 0); // Now, 'dst/dsize' assumes to have enough space always.

	p += u;

        cxt->buffer->cursor = cxt->buffer->buffer;
	cxt->buffer->used = 0;
	n = az_buffer_fetch_file(cxt->buffer, cxt->_fd, az_buffer_unused_bytes(cxt->buffer));
	if (n <= 0) {
	    m = az_buffer_push_back(cxt->buffer, p, dst + dsize - p);
	    fprintf(stderr, "cxt = %p (at %s)", cxt, cxt->filename);
	    assert(m == 0);
	    return n;
	}
    }

    p += u;
    *p = '\0';
    fprintf(stderr, "<<< _sc_follow_context_read_line (%s)\n", dst);
    return p - dst;
}


int
sc_follow_context_run(sc_follow_context* cxt, sc_message* msgbuf, sc_message** presp)
{
    // sc_message* msg = sc_message_new(csize), *resp = NULL;
    int ret = 0, cb = 0;
    off_t cur;

    assert(presp != NULL);
    *presp = NULL;

    fprintf(stderr, "context run\n");

    if (cxt->_fd <= 0) {
        sc_follow_context_open_file(cxt, 1);
        sc_follow_context_sync_file(cxt);
    }

    // cb = read(cxt->_fd, &msgbuf->content, BUFSIZE);
    cb = _sc_follow_context_read_line(cxt, msgbuf->content, BUFSIZE);
    if (cb == 0) {
        // sleep(1);
	// continue;
	/*
        msgbuf->code    = htons(SCM_MSG_DELE);
        msgbuf->channel = htons(cxt->channel);
        msgbuf->length  = 0;

	_sc_follow_context_proc_dele(cxt, msgbuf, presp);
	fprintf(stderr, "DELE: done\n");
	if (htons((*presp)->code) == SCM_RESP_OK) {
	    cxt->channel = 0;

	    sc_follow_context_close_file(cxt);
	}
	*/
	return -1001;
    } else if (cb < 0) {
        return -1;
    }

    // cxt->current_position = lseek(cxt->_fd, 0, SEEK_CUR);
    cur = lseek(cxt->_fd, 0, SEEK_CUR);

    assert(cxt->channel != 0);

    fprintf(stderr, "reading file...\n");

    msgbuf->code    = htons(SCM_MSG_DATA);
    msgbuf->channel = htons(cxt->channel);
    msgbuf->length  = htonl(cb);

    if (_sc_follow_context_proc_data(cxt, msgbuf, presp) != 0) {
	// reconnect
	fprintf(stderr, "reconnect now\n");
	sc_aggregator_connection_open(cxt->connection);
        sc_follow_context_sync_file(cxt);
	// reconnected
	return 1;
    }

    if (htons((*presp)->code) == SCM_RESP_OK) {
        cxt->current_position = cur;
    }

    return 0;
}

void
sc_follow_context_destroy(sc_follow_context* cxt)
{
    free(cxt->buffer);
    free(cxt->filename);
    free(cxt->displayname);
    free(cxt);
}

az_list* g_context_list;

int
main(int argc, char** argv)
{
    int epfd;

    sc_follow_context *cxt = NULL;
    sc_aggregator_connection *conn = NULL;

    int ret, ch, i;
    sc_message *msg, *resp;

    struct option long_opts[] = {
        { "server-port", 2, NULL, 0 },
        { "server-addr", 2, NULL, 0 },
    };

    while ((ch = getopt_long(argc, argv, "p:s:", long_opts, NULL)) != -1) {
        switch (ch) {
        case 'p':
            g_config_server_port = strtoul(optarg, NULL, 10);
            break;
        case 's':
            g_config_server_addr = strdup(optarg);
            break;
        }
    }
    argc -= optind;
    argv += optind;

    if (!g_config_server_addr) {
        fprintf(stderr, "usage: server address is not assigned.\n");
        exit(1);
    }

    conn = sc_aggregator_connection_new(g_config_server_addr, g_config_server_port);
    sc_aggregator_connection_open(conn);
    fprintf(stderr, "conn = %p\n", conn);

    for (i = 0; i < argc; i++) {
        char *fname = argv[i], *dispname = argv[i];

        // haha
        if (dispname[0] == '.' || dispname[0] == '/') {
            dispname = basename(dispname);
        }
        fprintf(stderr, "fname = [%s]\n", fname);
        fprintf(stderr, "dispname = [%s]\n", dispname);

        cxt = sc_follow_context_new(fname, dispname, conn);
        sc_follow_context_open_file(cxt, 0);

        sc_follow_context_sync_file(cxt);

        g_context_list = az_list_add(g_context_list, cxt);
    }

    msg = sc_message_new(BUFSIZE);
    while (1) {
        az_list* li;
	int sl = 1;

	for (li = g_context_list; li; li = li->next) {
            resp = NULL;
            ret = sc_follow_context_run(li->object, msg, &resp);
	    if (ret == -1001) {
	        sl = 1;
	    } else if (ret == -1) {
	        perror("sc_follow_context_run");
	        exit(1);
	    } else {
	        sl = 0;
	    }

            // here, we proceed response from aggregator
            sc_message_destroy(resp);
	}
	if (sl > 0) {
	    sleep(sl);
	    continue;
	}
    }
    sc_message_destroy(msg);
}
