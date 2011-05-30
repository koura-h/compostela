/* $Id$ */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <assert.h>
#include <getopt.h>
#include <libgen.h>
#include <time.h>
#include <byteswap.h>

#include "azlist.h"
#include "azbuffer.h"

#include "scmessage.h"
#include "supports.h"

#include "appconfig.h"
#include "config.h"

#include "sclog.h"


/////

int
setup_server_unix_socket(const char* path)
{
    int ss;
    struct sockaddr_un sun;

    ss = socket(PF_UNIX, SOCK_STREAM, 0);
    if (ss == -1) {
        perror("socket");
        return -1;
    }

    if (set_non_blocking(ss) == -1) {
        return -1;
    }

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = PF_UNIX;
    strcpy(sun.sun_path, path);
    unlink(path);

    if (bind(ss, (struct sockaddr*)&sun, sizeof(sun)) != 0) {
        perror("bind");
        goto on_error;
    }
    if (listen(ss, 5) != 0) {
        perror("listen");
        goto on_error;
    }

    return ss;

on_error:
    close(ss);
    return -1;
}

/////


typedef struct _sc_controller {
    int socket_fd;
    char *displayName;
    //
    az_buffer* buffer;
} sc_controller;


sc_controller*
sc_controller_new(int cc)
{
    sc_controller* c = (sc_controller*)malloc(sizeof(sc_controller));
    if (c) {
        memset(c, 0, sizeof(sc_controller));
        c->socket_fd = cc;
        c->buffer = az_buffer_new(2048);
    }
    return c;
}

void
sc_controller_destroy(sc_controller* c)
{
    az_buffer_destroy(c->buffer);
    assert(c->socket_fd == -1);
    free(c);
}

az_list* g_controller_list = NULL;

/////


enum { BUFSIZE = 8192 };


typedef struct _sc_follow_context {
    char *filename;
    int channel;
    // off_t current_position;
    off_t filesize;
    mode_t mode;
    int _fd;
    //
    az_buffer* buffer;
    //
    sc_message* message_buffer;
    //
    char *displayName;
    //
    int ftimestamp;
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
        conn->socket = -1;
	conn->host   = strdup(host);
	conn->port   = port;
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
        sc_log(LOG_DEBUG, "getaddrinfo: %s", gai_strerror(err));
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
sc_aggregator_connection_is_opened(sc_aggregator_connection* conn)
{
    return conn->socket != -1 ? 1 : 0;
}

int
sc_follow_context_sync_file(sc_follow_context *cxt)
{
    sc_message *msg, *resp;
    size_t n = strlen(cxt->displayName);
    int64_t stlen = 0;
    int32_t attr = 0, len = 0;

    sc_log(LOG_DEBUG, ">>> INIT: started");

    msg = sc_message_new(n + sizeof(int32_t));
    if (!msg) {
        return -1;
    }

    if (S_ISREG(cxt->mode) && !cxt->ftimestamp) {
        attr |= 0x80000000;
    }

    msg->code    = htons(SCM_MSG_INIT);
    msg->channel = htons(0);
    msg->length  = htonl(n + sizeof(int32_t));
    *(int32_t*)(&msg->content) = htonl(attr);
    memcpy(msg->content + sizeof(int32_t), cxt->displayName, n);

    // send_message
    if (sc_aggregator_connection_send_message(cxt->connection, msg) != 0) {
	sc_log(LOG_DEBUG, "INIT: connection has broken.");
        return -1;
    }

    if (sc_aggregator_connection_receive_message(cxt->connection, &resp) != 0) {
	sc_log(LOG_DEBUG, "INIT: connection has broken. (on receiving)");
        return -3;
    }

    if (ntohs(resp->code) != SCM_RESP_OK) {
        sc_log(LOG_DEBUG, ">>> INIT: failed (code=%d)", ntohs(resp->code));
        return -4;
    }
    cxt->channel = htons(resp->channel);
    len = htonl(resp->length);
    stlen = *(int64_t*)(&resp->content);
#if __BYTE_ORDER == __LITTLE_ENDIAN
    stlen = bswap_64(stlen);
#endif
    sc_log(LOG_DEBUG, ">>> INIT: len = %d", len);
    if (len > sizeof(int64_t)) {
        unsigned char* buf, *p;
	size_t bufsize, psize;

	p = resp->content + sizeof(int64_t);
	psize = len - sizeof(int64_t);

	mhash_with_size(cxt->filename, stlen, &buf, &bufsize);
	if (buf) {
	    if (psize != bufsize || memcmp(p, buf, bufsize) != 0) {
	        sc_log(LOG_DEBUG, "mhash invalid!!!");
		exit(-1);
	    } else {
	        sc_log(LOG_DEBUG, "mhash check: OK");
	    }
	    free(buf);
	} else {
	    sc_log(LOG_DEBUG, "mhash not found");
	}
    }
    sc_log(LOG_DEBUG, "channel id = %d", cxt->channel);
    sc_log(LOG_DEBUG, "stlen = %d", stlen);
    lseek(cxt->_fd, stlen, SEEK_SET);

    sc_log(LOG_DEBUG, ">>> INIT: finished");
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

    sc_log(LOG_DEBUG, "send_message: code = %d, channel = %d, length = %ld", ntohs(msg->code), ntohs(msg->channel), len);
    if ((ret = sendall(conn->socket, msg, len + offsetof(sc_message, content), 0)) <= 0) {
        close(conn->socket);
	conn->socket = -1;
        perror("sendall");
        sc_log(LOG_DEBUG, "sending error");
        return -1;
    }
    sc_log(LOG_DEBUG, "send_message: done");

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
        close(conn->socket);
	conn->socket = -1;
        perror("recvall");
        return -1;
    } else if (n == 0) {
        sc_log(LOG_DEBUG, "closed");
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
_sc_follow_context_init(sc_follow_context* cxt, const char* dispname, int ftimestamp, sc_aggregator_connection* conn)
{
    memset(cxt, 0, sizeof(sc_follow_context));

    cxt->connection = conn;
    cxt->_fd = -1;

    if (dispname) {
        cxt->displayName = strdup(dispname);
        if (!cxt->displayName) {
            free(cxt->filename);
            free(cxt);
            return NULL;
        }
    }
    // we should read control files for 'fname'

    cxt->ftimestamp = ftimestamp;

    cxt->buffer = az_buffer_new(BUFSIZE);
    cxt->message_buffer = sc_message_new(BUFSIZE);
    cxt->message_buffer->code = htons(SCM_MSG_NONE);

    return cxt;
}

void
sc_follow_context_destroy(sc_follow_context* cxt);

sc_follow_context*
sc_follow_context_new(const char* fname, const char* dispname, int ftimestamp, sc_aggregator_connection* conn)
{
    sc_follow_context* cxt = NULL;

    cxt = (sc_follow_context*)malloc(sizeof(sc_follow_context));
    if (cxt = _sc_follow_context_init(cxt, dispname, ftimestamp, conn)) {
        if (fname) {
            cxt->filename = strdup(fname);
	    if (!cxt->filename) {
                sc_follow_context_destroy(cxt);
                return NULL;
	    }
        }
    }

    return cxt;
}

sc_follow_context*
sc_follow_context_new_with_fd(int fd, const char* dispname, int ftimestamp, sc_aggregator_connection* conn)
{
    sc_follow_context* cxt = NULL;

    cxt = (sc_follow_context*)malloc(sizeof(sc_follow_context));
    if (cxt = _sc_follow_context_init(cxt, dispname, ftimestamp, conn)) {
        cxt->_fd = fd;
    }

    return cxt;
}

int
sc_follow_context_open_file(sc_follow_context* cxt)
{
    struct stat st;
    int flags;

    cxt->_fd = open(cxt->filename, O_RDONLY | O_NONBLOCK);
    if (cxt->_fd < 0) {
        sc_log(LOG_DEBUG, ">>> %s: error (%d)", __FUNCTION__, errno);
        return -1;
    }

    fstat(cxt->_fd, &st);
    cxt->filesize = st.st_size;
    cxt->mode = st.st_mode;

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

    cxt->mode = 0;
    cxt->filesize = 0;

    return 0;
}

void
sc_follow_context_reset(sc_follow_context* cxt)
{
    sc_follow_context_close_file(cxt);
    az_buffer_reset(cxt->buffer);
    cxt->message_buffer->code = htons(SCM_MSG_NONE);
}

int
_sc_follow_context_proc_data(sc_follow_context* cxt, sc_message* msg, sc_message** ppresp)
{
    int ret;
    *ppresp = NULL;

    if ((ret = sc_aggregator_connection_send_message(cxt->connection, msg)) != 0) {
	// connection broken
	sc_log(LOG_DEBUG, "DATA: connection has broken.");
	return ret;
    }

    if ((ret = sc_aggregator_connection_receive_message(cxt->connection, ppresp)) != 0) {
	sc_log(LOG_DEBUG, "DATA: connection has broken. (on receiving) = %d", ret);
	return ret;
    }

    return ret;
}

int
_sc_follow_context_proc_rele(sc_follow_context* cxt, sc_message* msg, sc_message** ppresp)
{
    int ret = 0;
    *ppresp = NULL;

    if ((ret = sc_aggregator_connection_send_message(cxt->connection, msg)) != 0) {
	// connection broken
	sc_log(LOG_DEBUG, "RELE: connection has broken.");
	return ret;
    }

    if ((ret = sc_aggregator_connection_receive_message(cxt->connection, ppresp)) != 0) {
	sc_log(LOG_DEBUG, "RELE: connection has broken. (on receiving) = %d", ret);
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

    sc_log(LOG_DEBUG, ">>> _sc_follow_context_read_line");

    if (az_buffer_unread_bytes(cxt->buffer) == 0) {
        az_buffer_reset(cxt->buffer);
        n = az_buffer_fetch_file(cxt->buffer, cxt->_fd, az_buffer_unused_bytes(cxt->buffer));
	if (n <= 0) {
            if (errno == EAGAIN) { // for read()
                return 0;
            }
	    return n;
	}
    }

    while ((n = az_buffer_read_line(cxt->buffer, p, dst + dsize - p, &u)) != 0) {
        assert(n > 0); // Now, 'dst/dsize' assumes to have enough space always.

	p += u;

        az_buffer_reset(cxt->buffer);
	n = az_buffer_fetch_file(cxt->buffer, cxt->_fd, az_buffer_unused_bytes(cxt->buffer));
	if (n <= 0) {
            if (errno == EAGAIN) { // for read()
                n = 0;
            }
	    m = az_buffer_push_back(cxt->buffer, p, dst + dsize - p);
	    sc_log(LOG_DEBUG, "cxt = %p (at %s)", cxt, cxt->filename);
	    assert(m == 0);
	    return n;
	}
    }

    p += u;
    *p = '\0';
    sc_log(LOG_DEBUG, "<<< _sc_follow_context_read_line (%s)", dst);
    return p - dst;
}

int
sc_follow_context_close(sc_follow_context* cxt)
{
    sc_message* msg = NULL, *resp = NULL;
    int ret;

    sc_log(LOG_DEBUG, "context close");
    if (!sc_aggregator_connection_is_opened(cxt->connection)) {
        // disconnected. but show must go on.
	sc_log(LOG_DEBUG, ">>> %s: PLEASE RECONNECT NOW!", __FUNCTION__);
	return 1001;
    }

    if (cxt->_fd < 0) {
        sc_log(LOG_DEBUG, "already closed.");
	return -1;
    }

    msg = sc_message_new(sizeof(int32_t));
    msg->code    = htons(SCM_MSG_RELE);
    msg->channel = htons(cxt->channel);
    msg->length  = htonl(sizeof(int32_t));
    memset(msg->content, 0, sizeof(int32_t));

    if ((ret = sc_aggregator_connection_send_message(cxt->connection, msg)) != 0) {
	// connection broken
	sc_log(LOG_DEBUG, "RELE: connection has broken.");
	sc_message_destroy(msg);
	return 1001;
    }

    if ((ret = sc_aggregator_connection_receive_message(cxt->connection, &resp)) != 0) {
	sc_log(LOG_DEBUG, "RELE: connection has broken. (on receiving) = %d", ret);
	sc_message_destroy(msg);
	return 1001;
    }
    sc_message_destroy(msg);

    return 0;
}

/**
 *
 * returns: >0 ... no data processed, or connection to aggregator is down.
 *                 retry later.
 *          =0 ... data processed, and sent to aggregator. everything's good.
 *          <0 ... error occurred.
 */
int
sc_follow_context_run(sc_follow_context* cxt, sc_message** presp)
{
    // sc_message* msg = sc_message_new(csize), *resp = NULL;
    int ret = 0, cb = 0, cb0 = 0;
    off_t cur;

    assert(presp != NULL);
    *presp = NULL;

    sc_message* msgbuf = cxt->message_buffer;

    sc_log(LOG_DEBUG, "context run");
    if (!sc_aggregator_connection_is_opened(cxt->connection)) {
        // disconnected. but show must go on.
	sc_log(LOG_DEBUG, ">>> %s: PLEASE RECONNECT NOW!", __FUNCTION__);
	return 1001;
    }

    if (msgbuf->code == htons(SCM_MSG_NONE)) {
        if (cxt->_fd <= 0) {
            if (sc_follow_context_open_file(cxt) != 0) {
                sc_log(LOG_DEBUG, "sc_follow_context_run: not opened yet => [%s]", cxt->filename);
                return 1;
            }
            sc_follow_context_sync_file(cxt);
        }

        if (cxt->ftimestamp) {
            time_t t;
            time(&t);
            cb0 = __w3cdatetime(msgbuf->content, BUFSIZE, t);

            msgbuf->content[cb0++] = ' ';
            msgbuf->content[cb0] = '\0';
        }

        cb = _sc_follow_context_read_line(cxt, msgbuf->content + cb0, BUFSIZE - cb0);
        if (cb == 0) {
            // EOF, wait for the new available data.
	    return 1;
        } else if (cb < 0) {
            return -1;
        }

        assert(cxt->channel != 0);
        sc_log(LOG_DEBUG, "reading file...");

        msgbuf->code    = htons(SCM_MSG_DATA);
        msgbuf->channel = htons(cxt->channel);
        msgbuf->length  = htonl(cb0 + cb);
    }

    if (_sc_follow_context_proc_data(cxt, msgbuf, presp) != 0) {
	// should reconnect
	sc_log(LOG_DEBUG, "You should reconnect now");
#if 0
	sc_aggregator_connection_open(cxt->connection);
        sc_follow_context_sync_file(cxt);
#endif
	return 1001;
    }

    if (htons((*presp)->code) == SCM_RESP_OK) {
        // cxt->current_position = cur;
	msgbuf->code = htons(SCM_MSG_NONE);
    }

    return 0;
}

void
sc_follow_context_destroy(sc_follow_context* cxt)
{
    sc_message_destroy(cxt->message_buffer);
    az_buffer_destroy(cxt->buffer);

    free(cxt->filename);
    free(cxt->displayName);
    free(cxt);
}

az_list* g_context_list = NULL;

const char *DEFAULT_CONF = PATH_SYSCONFDIR "/comfollower.conf";
#define PATH_CONTROL "/tmp/comfollower.sock"

double
get_seconds_left_in_today()
{
    struct tm tm0, tm1;
    time_t t, t1;

    time(&t);
    localtime_r(&t, &tm0);
    t1 = t + 86400;
    localtime_r(&t1, &tm1);
    tm1.tm_hour = tm1.tm_min = tm1.tm_sec = 0;
    t1 = mktime(&tm1);

    return difftime(t1, t);
}

/////

int
_do_receive_data(int c, const void *data, size_t dlen, void* info)
{
    sc_controller* contr = (sc_controller*)info;
    char line[2048];
    size_t u;
    int n;

#if 0
    if (contr->direct) {
        
    }
#endif

    sc_log(LOG_DEBUG, "data = %p, dlen = %d", data, dlen);

    if (az_buffer_unread_bytes(contr->buffer) == 0) {
        az_buffer_reset(contr->buffer);
    }
    n = az_buffer_fetch_bytes(contr->buffer, data, dlen);

    while ((n = az_buffer_read_line(contr->buffer, line, sizeof(line), &u)) != 0) {
        assert(n > 0); // Now, 'dst/dsize' assumes to have enough space always.

        line[u] = '\0';

        fprintf(stdout, "line = [%s]\n", line);
    }

    return 0;
}

enum { MAX_EVENTS = 16 };

int
do_receive(int epfd, int c, void* info)
{
    sc_controller* contr = (sc_controller*)info;
    ssize_t n;
    int16_t code = 0;

    sc_log(LOG_DEBUG, ">>> do_receive");

    unsigned char* databuf = (unsigned char*)malloc(BUFSIZE);
    
    n = recv(c, databuf, BUFSIZE, 0);
    if (n > 0) {
        _do_receive_data(c, databuf, n, contr);
    } else if (n == 0) {
        struct epoll_event ev;

        sc_log(LOG_DEBUG, "connection closed");

        epoll_ctl(epfd, EPOLL_CTL_DEL, c, NULL);

        close(c);

        sc_controller_destroy(contr);
    } else {
        struct epoll_event ev;

        sc_log(LOG_DEBUG, "recvall error.");

        epoll_ctl(epfd, EPOLL_CTL_DEL, c, NULL);

        close(c);

        sc_controller_destroy(contr);
    }

    sc_log(LOG_DEBUG, "<<< do_receive");

    free(databuf);
    return 0;
}


int
setup_epoll(int* socks, int num_socks)
{
    int epfd, i;
    struct epoll_event ev;

    if ((epfd = epoll_create(MAX_EVENTS)) < 0) {
        sc_log(LOG_DEBUG, "epoll_create error");
        return -1;
    }

    for (i = 0; i < num_socks; i++) {
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        ev.data.fd = socks[i];
        epoll_ctl(epfd, EPOLL_CTL_ADD, socks[i], &ev);
    }

    return epfd;
}

// run_main(int* socks, int num_socks)
int
do_server_socket(int epfd, int* socks, int num_socks)
{
    struct epoll_event ev, events[MAX_EVENTS];
    int i, j;

    ssize_t n;
    sc_controller* contr = NULL;

    int done = 0;

    // for (;;) {
    {
        int nfd, c = -1;

        nfd = epoll_wait(epfd, events, MAX_EVENTS, 0);
        for (i = 0; i < nfd; i++) {
            done = 0;
            for (j = 0; j < num_socks; j++) {
                if (events[i].data.fd == socks[j]) {
                    struct sockaddr_storage ss;
                    socklen_t sslen = sizeof(ss);
                    int err;

                    memset(&ss, 0, sizeof(ss));
                    c = accept(socks[j], (struct sockaddr*)&ss, &sslen);
                    if (c < 0) {
                        if (errno == EAGAIN) {
                            continue;
                        } else {
                            perror("accept");
                        }
                    }

                    sc_log(LOG_DEBUG, "accepted");

                    // conn = sc_connection_new((struct sockaddr*)&ss, sslen, c);
                    contr = sc_controller_new(c);

                    set_non_blocking(c);
                    ev.events = EPOLLIN | EPOLLET;
                    // ev.data.ptr = conn;
                    ev.data.ptr = contr;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, c, &ev) < 0) {
                        sc_log(LOG_DEBUG, "epoll set insertion error: fd = %d", c);
                        continue;
                    }
                    done = 1;
                    break;
                }
            }

            if (!done) {
                // sc_connection* conn = events[i].data.ptr;
                contr = events[i].data.ptr;
                do_receive(epfd, contr->socket_fd, contr);
            }
        }
    }
}

/////

void
usage()
{
    fprintf(stdout, "USAGE: comfollower\n");
}

static sc_aggregator_connection *g_connection = NULL;
static int g_conn_controller = -1;

int
main(int argc, char** argv)
{
    sc_follow_context *cxt = NULL;

    int ret, ch, i, epfd;
    sc_message *resp;
    char *conf = NULL;

    struct option long_opts[] = {
        { "config", 2, NULL, 0 },
        { "server-port", 2, NULL, 0 },
        { "server-addr", 2, NULL, 0 },
        { "help", 2, NULL, 0 },
    };

    while ((ch = getopt_long(argc, argv, "c:p:s:h", long_opts, NULL)) != -1) {
        switch (ch) {
	case 'c':
	    conf = strdup(optarg);
	    break;
        case 'p':
            g_config_server_port = strtoul(optarg, NULL, 10);
            break;
        case 's':
            g_config_server_address = strdup(optarg);
            break;
        case 'h':
        default:
            usage();
            exit(1);
        }
    }
    argc -= optind;
    argv += optind;

    load_config_file((conf ? conf : DEFAULT_CONF));
    free(conf);

    if (!g_config_server_address) {
        sc_log(LOG_DEBUG, "error: server address is not assigned.");
        exit(1);
    }

    g_connection = sc_aggregator_connection_new(g_config_server_address, g_config_server_port);
    sc_aggregator_connection_open(g_connection);
    sc_log(LOG_DEBUG, "conn = %p", g_connection);

    g_conn_controller = setup_server_unix_socket(PATH_CONTROL);

    do_rotate(g_connection);
    set_rotation_timer();
    set_sigpipe_handler();

    epfd = setup_epoll(&g_conn_controller, 1);

    while (1) {
        az_list* li;
	int sl = 1, rc = 0, cc;
        cxt = NULL;

        fprintf(stdout, ".");

        do_server_socket(epfd, &g_conn_controller, 1);

	for (li = g_context_list; li; li = li->next) {
            resp = NULL;
            cxt = li->object;
            ret = sc_follow_context_run(cxt, &resp);
	    if (ret > 0) {
	        if (ret >= 1000) {
		    rc = 1;
		}
	    } else if (ret == -1) {
	        // error occurred
	        perror("sc_follow_context_run");
	        exit(1);
	    } else {
	        // in proceessed any bytes.
	        sl = 0;
	    }

            // here, we proceed response from aggregator
            sc_message_destroy(resp);
	}

	if (rc) {
	    for (li = g_context_list; li; li = li->next) {
                cxt = li->object;
                // haha
                sc_follow_context_reset(cxt);
            }
	    sc_aggregator_connection_open(g_connection);
	}

	if (sl > 0) {
	    sleep(sl);
	    continue;
	}
    }
}

void
handler_alarm(int sig, siginfo_t* sinfo, void* ptr)
{
    struct itimerval itimer = {};
    sc_log(LOG_DEBUG, ">>> %s: BEGIN", __FUNCTION__);

    do_rotate(g_connection);

#if 0
    itimer.it_interval.tv_sec = 86400;
#else
    itimer.it_interval.tv_sec = 1;
#endif
    itimer.it_interval.tv_usec = 0;
    itimer.it_value = itimer.it_interval;
    assert(setitimer(ITIMER_REAL, &itimer, 0) == 0);

    sc_log(LOG_DEBUG, ">>> %s: END", __FUNCTION__);
}

int
set_rotation_timer()
{
    double secs_left = 0.0L;
    struct itimerval itimer = {};
    struct sigaction sa = {
        .sa_sigaction = handler_alarm,
        .sa_flags = SA_RESTART | SA_SIGINFO,
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

#if 0
    secs_left = get_seconds_left_in_today();
    sc_log(LOG_DEBUG, "secs_left = %lf", secs_left);

    itimer.it_interval.tv_sec = secs_left;
#else
    itimer.it_interval.tv_sec = 1;
#endif
    itimer.it_interval.tv_usec = 0;
    itimer.it_value = itimer.it_interval;
    assert(setitimer(ITIMER_REAL, &itimer, 0) == 0);

    return 0;
}

int
do_rotate(sc_aggregator_connection* conn)
{
    sc_config_pattern_entry *pe;
    struct tm tm;
    time_t t;
    sc_follow_context* cxt = NULL;
    int not_found;
    az_list *li, *lj;

    time(&t);
    localtime_r(&t, &tm);
    for (pe = g_config_patterns; pe; pe = pe->_next) {
        char fn[PATH_MAX], dn[PATH_MAX];

        if (pe->rotate && strchr(pe->path, '%')) {
	    strftime(fn, sizeof(fn), pe->path, &tm);
	} else {
	    strncpy(fn, pe->path, sizeof(fn));
	}

	if (pe->displayName) {
	    if (pe->rotate && strchr(pe->displayName, '%')) {
	        strftime(dn, sizeof(dn), pe->displayName, &tm);
	    } else {
	        strncpy(dn, pe->displayName, sizeof(dn));
	    }
	} else {
	    strcpy(dn, basename(fn));
	}
        sc_log(LOG_DEBUG, "fname = [%s] / dispname = [%s]", fn, dn);

        not_found = 1;
        for (li = g_context_list; not_found && li; li = li->next) {
            cxt = li->object;
            if (strcmp(cxt->filename, fn) == 0) {
		if (strcmp(cxt->displayName, dn) == 0) {
                    // ok, I'm already following it.
                    not_found = 0;
		} else {
		    // displayName has rotated.
		    sc_follow_context_close(cxt);
		}
                sc_log(LOG_DEBUG, "=== already following now.", fn, dn);
                break;
            }
        }

        if (not_found) {
            cxt = sc_follow_context_new(fn, dn, pe->append_timestamp, conn);
            g_context_list = az_list_add(g_context_list, cxt);
            sc_log(LOG_DEBUG, "added: new follow_context");
        }
    }

    for (li = g_controller_list; li; li = li->next) {
        char dn[PATH_MAX];
        sc_controller* contr = li->object;

        if (contr->displayName == NULL) {
            continue;
        }

        if (strchr(contr->displayName, '%')) {
	    strftime(dn, sizeof(dn), contr->displayName, &tm);
        } else {
            strncpy(dn, contr->displayName, sizeof(dn));
        }

        not_found = 1;
        for (lj = g_context_list; not_found && lj; lj = lj->next) {
            cxt = lj->object;
            if (cxt->_fd == contr->socket_fd) {
                if (strcmp(cxt->displayName, dn) == 0) {
                    not_found = 0;
                } else {
                    sc_follow_context_close(cxt);
                }
            }
        }

        if (not_found) {
            cxt = sc_follow_context_new_with_fd(contr->socket_fd, dn, pe->append_timestamp, conn);
            g_context_list = az_list_add(g_context_list, cxt);
            sc_log(LOG_DEBUG, "added: new follow_context => displayName: %s", dn);
        }
    }
}
