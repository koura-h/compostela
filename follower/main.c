/* $Id$ */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
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


enum { BUFSIZE = 8196 };


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
sc_aggregator_connection_is_opened(sc_aggregator_connection* conn)
{
    return conn->socket != -1 ? 1 : 0;
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
	fprintf(stderr, "INIT: connection has broken.\n");
        return -1;
    }

    if (sc_aggregator_connection_receive_message(cxt->connection, &resp) != 0) {
	fprintf(stderr, "INIT: connection has broken. (on receiving)\n");
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
        close(conn->socket);
	conn->socket = -1;
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
        close(conn->socket);
	conn->socket = -1;
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
	cxt->message_buffer = sc_message_new(BUFSIZE);
	cxt->message_buffer->code = htons(SCM_MSG_NONE);
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
        fprintf(stderr, ">>> %s: error (%d)\n", __FUNCTION__, errno);
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
	fprintf(stderr, "DATA: connection has broken.\n");
	return ret;
    }

    if ((ret = sc_aggregator_connection_receive_message(cxt->connection, ppresp)) != 0) {
	fprintf(stderr, "DATA: connection has broken. (on receiving) = %d\n", ret);
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
	fprintf(stderr, "RELE: connection has broken.\n");
	return ret;
    }

    if ((ret = sc_aggregator_connection_receive_message(cxt->connection, ppresp)) != 0) {
	fprintf(stderr, "RELE: connection has broken. (on receiving) = %d\n", ret);
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
            if (errno == EAGAIN) { // for read()
                return 0;
            }
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
            if (errno == EAGAIN) { // for read()
                n = 0;
            }
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
sc_follow_context_close(sc_follow_context* cxt)
{
    sc_message* msg = NULL, *resp = NULL;
    int ret;

    fprintf(stderr, "context close\n");
    if (!sc_aggregator_connection_is_opened(cxt->connection)) {
        // disconnected. but show must go on.
	fprintf(stderr, ">>> %s: PLEASE RECONNECT NOW!\n", __FUNCTION__);
	return 1001;
    }

    if (cxt->_fd < 0) {
        fprintf(stderr, "already closed.\n");
	return -1;
    }

    msg = sc_message_new(sizeof(int32_t));
    msg->code   = htons(SCM_MSG_RELE);
    msg->code   = htons(cxt->channel);
    msg->length = htonl(sizeof(int32_t));
    memset(msg->content, 0, sizeof(int32_t));

    if ((ret = sc_aggregator_connection_send_message(cxt->connection, msg)) != 0) {
	// connection broken
	fprintf(stderr, "RELE: connection has broken.\n");
	sc_message_destroy(msg);
	return 1001;
    }

    if ((ret = sc_aggregator_connection_receive_message(cxt->connection, &resp)) != 0) {
	fprintf(stderr, "RELE: connection has broken. (on receiving) = %d\n", ret);
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
    int ret = 0, cb = 0;
    off_t cur;

    assert(presp != NULL);
    *presp = NULL;

    sc_message* msgbuf = cxt->message_buffer;

    fprintf(stderr, "context run\n");
    if (!sc_aggregator_connection_is_opened(cxt->connection)) {
        // disconnected. but show must go on.
	fprintf(stderr, ">>> %s: PLEASE RECONNECT NOW!\n", __FUNCTION__);
	return 1001;
    }

    if (msgbuf->code == htons(SCM_MSG_NONE)) {
        if (cxt->_fd <= 0) {
            if (sc_follow_context_open_file(cxt) != 0) {
                fprintf(stderr, "sc_follow_context_run: not opened yet => [%s]\n", cxt->filename);
                return 1;
            }
            sc_follow_context_sync_file(cxt);
        }

        cb = _sc_follow_context_read_line(cxt, msgbuf->content, BUFSIZE);
        if (cb == 0) {
            // EOF, wait for the new available data.
	    return 1;
        } else if (cb < 0) {
            return -1;
        }

        assert(cxt->channel != 0);
        fprintf(stderr, "reading file...\n");

        msgbuf->code    = htons(SCM_MSG_DATA);
        msgbuf->channel = htons(cxt->channel);
        msgbuf->length  = htonl(cb);
    }

    if (_sc_follow_context_proc_data(cxt, msgbuf, presp) != 0) {
	// should reconnect
	fprintf(stderr, "You should reconnect now\n");
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
    free(cxt->displayname);
    free(cxt);
}

az_list* g_context_list = NULL;

const char *DEFAULT_CONF = PATH_SYSCONFDIR "/comfollower.conf";

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

static sc_aggregator_connection *g_connection = NULL;

int
main(int argc, char** argv)
{
    int epfd;

    sc_follow_context *cxt = NULL;

    int ret, ch, i;
    sc_message *resp;
    char *conf = NULL;

    struct option long_opts[] = {
        { "config", 2, NULL, 0 },
        { "server-port", 2, NULL, 0 },
        { "server-addr", 2, NULL, 0 },
    };

    while ((ch = getopt_long(argc, argv, "c:p:s:", long_opts, NULL)) != -1) {
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
        }
    }
    argc -= optind;
    argv += optind;

    load_config_file((conf ? conf : DEFAULT_CONF));
    free(conf);

    if (!g_config_server_address) {
        fprintf(stderr, "usage: server address is not assigned.\n");
        exit(1);
    }

    g_connection = sc_aggregator_connection_new(g_config_server_address, g_config_server_port);
    sc_aggregator_connection_open(g_connection);
    fprintf(stderr, "conn = %p\n", g_connection);

    do_rotate(g_connection);
    set_rotation_timer();
    set_sigpipe_handler();

    while (1) {
        az_list* li;
	int sl = 1, rc = 0;
        cxt = NULL;

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
    fprintf(stderr, ">>> %s: BEGIN\n", __FUNCTION__);

    do_rotate(g_connection);

    itimer.it_interval.tv_sec = 86400;
    itimer.it_interval.tv_usec = 0;
    itimer.it_value = itimer.it_interval;
    assert(setitimer(ITIMER_REAL, &itimer, 0) == 0);

    fprintf(stderr, ">>> %s: END\n", __FUNCTION__);
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

    secs_left = get_seconds_left_in_today();
    fprintf(stderr, "secs_left = %lf\n", secs_left);

    itimer.it_interval.tv_sec = secs_left;
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
    az_list *li;

    // connection must be opened.

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
        fprintf(stderr, "fname = [%s]\n", fn);
        fprintf(stderr, "dispname = [%s]\n", dn);

        not_found = 1;
        for (li = g_context_list; not_found && li; li = li->next) {
            cxt = li->object;
            if (strcmp(cxt->filename, fn) == 0) {
		if (strcmp(cxt->displayname, dn) == 0) {
                    // ok, I'm already following it.
                    not_found = 0;
		} else {
		    // displayName has rotated.
		    sc_follow_context_close(cxt);
		}
                fprintf(stderr, "yeah, file(%s) has been already following now.\n");
                break;
            }
        }

        if (not_found) {
            cxt = sc_follow_context_new(fn, dn, conn);

            g_context_list = az_list_add(g_context_list, cxt);
        }
    }
}
