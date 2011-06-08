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
#include "azlog.h"

#include "message.h"
#include "connection.h"
#include "follow_context.h"

#include "appconfig.h"
#include "supports.h"

#include "config.h"

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
    int f_direct;
    //
    az_buffer_ref buffer;
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

/////

static sc_aggregator_connection_ref g_connection = NULL;
static int g_conn_controller = -1;

/////
static int
_init_file(sc_follow_context *cxt)
{
    sc_log_message *msg, *resp;
    size_t n = strlen(cxt->displayName);
    int64_t pos = 0;
    int32_t attr = 0, len = 0;

    az_log(LOG_DEBUG, ">>> INIT: started");

    msg = sc_log_message_new(n + sizeof(int32_t));
    if (!msg) {
        return -1;
    }

    if (S_ISREG(cxt->mode) && !cxt->ftimestamp) {
        attr |= 0x80000000;
    }

    msg->code    = SCM_MSG_INIT;
    msg->channel = 0;

    *(int32_t*)(&msg->content) = htonl(attr);
    memcpy(msg->content + sizeof(int32_t), cxt->displayName, n);

    // send_message
    if (sc_aggregator_connection_send_message(cxt->connection, msg) != 0) {
        az_log(LOG_DEBUG, "INIT: connection has broken.");
        return -1;
    }

    if (sc_aggregator_connection_receive_message(cxt->connection, &resp) != 0) {
        az_log(LOG_DEBUG, "INIT: connection has broken. (on receiving)");
        return -3;
    }

    if (resp->code != SCM_RESP_OK) {
        az_log(LOG_DEBUG, ">>> INIT: failed (code=%d)", resp->code);
        return -4;
    }

    cxt->channel = resp->channel;
    len = resp->content_length;

    pos = *(int64_t*)(&resp->content);
#if __BYTE_ORDER == __LITTLE_ENDIAN
    pos = bswap_64(pos);
#endif
    az_log(LOG_DEBUG, ">>> INIT: len = %d", len);
    if (len > sizeof(int64_t)) {
        unsigned char* buf, *p;
        size_t bufsize, psize;

        p = resp->content + sizeof(int64_t);
        psize = len - sizeof(int64_t);

        mhash_with_size(cxt->filename, pos, &buf, &bufsize);
        if (buf) {
            if (psize != bufsize || memcmp(p, buf, bufsize) != 0) {
                az_log(LOG_DEBUG, "mhash invalid!!!");
                exit(-1);
            } else {
                az_log(LOG_DEBUG, "mhash check: OK");
            }
            free(buf);
        } else {
            az_log(LOG_DEBUG, "mhash not found");
        }
    }
    cxt->position = pos;

    az_log(LOG_DEBUG, "INIT: cxt->channel = %d, cxt->position = %ld", cxt->channel, cxt->position);
    lseek(cxt->_fd, cxt->position, SEEK_SET);

    az_log(LOG_DEBUG, ">>> INIT: finished");
    return 0;
}

static int
_sync_file(sc_follow_context *cxt)
{
    sc_log_message *msg, *resp;
    size_t n = strlen(cxt->displayName);
    int64_t pos = 0;

    size_t size;
    unsigned char* p;    

    az_log(LOG_DEBUG, ">>> SYNC: started");

    mhash_with_size(cxt->filename, cxt->position, &p, &size);

    msg = sc_log_message_new(size);
    if (!msg) {
        return -1;
    }

    msg->code    = SCM_MSG_SYNC;
    msg->channel = cxt->channel;

    memcpy(msg->content, p, size);
    free(p);

    // send_message
    if (sc_aggregator_connection_send_message(cxt->connection, msg) != 0) {
        az_log(LOG_DEBUG, "SYNC: connection has bren.");
        return -1;
    }

    if (sc_aggregator_connection_receive_message(cxt->connection, &resp) != 0) {
        az_log(LOG_DEBUG, "SYNC: connection has bren. (on receiving)");
        return -3;
    }

    pos = *(int64_t*)(&resp->content);
#if __BYTE_ORDER == __LITTLE_ENDIAN
    pos = bswap_64(pos);
#endif

    if (resp->code != SCM_RESP_OK) {
        az_log(LOG_DEBUG, ">>> SYNC: failed (code=%d)", resp->code);
        cxt->position = pos;
    }
    az_log(LOG_DEBUG, "SYNC: cxt->position = %d", cxt->position);
    lseek(cxt->_fd, cxt->position, SEEK_SET);

    az_log(LOG_DEBUG, ">>> SYNC: finished");
    return 0;
}

static int
_sc_follow_context_proc_data(sc_follow_context* cxt, sc_log_message* msg, sc_log_message** ppresp)
{
    int ret;
    *ppresp = NULL;

    if ((ret = sc_aggregator_connection_send_message(cxt->connection, msg)) != 0) {
	// connection bren
	az_log(LOG_DEBUG, "DATA: connection has bren.");
	return ret;
    }

    if ((ret = sc_aggregator_connection_receive_message(cxt->connection, ppresp)) != 0) {
	az_log(LOG_DEBUG, "DATA: connection has bren. (on receiving) = %d", ret);
	return ret;
    }

    return ret;
}

static int
_sc_follow_context_proc_rele(sc_follow_context* cxt, sc_log_message* msg, sc_log_message** ppresp)
{
    int ret = 0;
    *ppresp = NULL;

    if ((ret = sc_aggregator_connection_send_message(cxt->connection, msg)) != 0) {
	// connection bren
	az_log(LOG_DEBUG, "RELE: connection has bren.");
	return ret;
    }

    if ((ret = sc_aggregator_connection_receive_message(cxt->connection, ppresp)) != 0) {
	az_log(LOG_DEBUG, "RELE: connection has bren. (on receiving) = %d", ret);
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

    az_log(LOG_DEBUG, ">>> _sc_follow_context_read_line");

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
	    m = az_buffer_push_back(cxt->buffer, p, u);
	    az_log(LOG_DEBUG, "cxt = %p (at %s)", cxt, cxt->filename);
	    assert(m == 0);
	    return n;
	}
    }

    p += u;
    *p = '\0';
    az_log(LOG_DEBUG, "<<< _sc_follow_context_read_line (%s)", dst);
    return p - dst;
}

/**
 *
 * returns: >0 ... no data processed, or connection to aggregator is down.
 *                 retry later.
 *          =0 ... data processed, and sent to aggregator. everything's good.
 *          <0 ... error occurred.
 */
static int
_run_follow_context(sc_follow_context* cxt, sc_log_message** presp)
{
    // sc_log_message* msg = sc_log_message_new(csize), *resp = NULL;
    int ret = 0, cb = 0, cb0 = 0;
    off_t cur;

    assert(presp != NULL);
    *presp = NULL;

    sc_log_message* msgbuf = cxt->message_buffer;

    az_log(LOG_DEBUG, "context run");
    if (!sc_aggregator_connection_is_opened(cxt->connection)) {
        // disconnected. but show must go on.
	az_log(LOG_DEBUG, ">>> %s: PLEASE RECONNECT NOW!", __FUNCTION__);
	return 1001;
    }

    if (msgbuf->code == SCM_MSG_NONE) {
        if (cxt->_fd <= 0) {
            if (sc_follow_context_open_file(cxt) != 0) {
                az_log(LOG_DEBUG, "sc_follow_context_run: not opened yet => [%s]", cxt->filename);
                return 1;
            }
            _init_file(cxt);
        }

        if (cxt->ftimestamp) {
            time_t t;
            time(&t);
            cb0 = __w3cdatetime(msgbuf->content, BUFSIZE, t);

            msgbuf->content[cb0++] = ':';
            msgbuf->content[cb0++] = ' ';
            msgbuf->content[cb0]   = '\0';
        }

        cb = _sc_follow_context_read_line(cxt, msgbuf->content + cb0, BUFSIZE - cb0);
        if (cb == 0) {
            // EOF, wait for the new available data.
	    return 1;
        } else if (cb < 0) {
            return -1;
        }

        // assert(cxt->channel != 0);
        if (cxt->channel == 0) {
            _init_file(cxt);
        }
        az_log(LOG_DEBUG, "reading file...");

        msgbuf->code           = SCM_MSG_DATA;
        msgbuf->channel        = cxt->channel;
        msgbuf->content_length = cb0 + cb;
    }

    if (_sc_follow_context_proc_data(cxt, msgbuf, presp) != 0) {
	// should reconnect
	az_log(LOG_DEBUG, "You should reconnect now");
	return 1001;
    }

    if ((*presp)->code == SCM_RESP_OK) {
        // cxt->current_position = cur;
	msgbuf->code = SCM_MSG_NONE;
    }

    return 0;
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
_do_controller_direct_open_with_line(char* linebuf, size_t size, sc_controller* contr)
{
    sc_follow_context* cxt = NULL;
    char *p = linebuf + 5, *px;

    for (px = p; *px; px++) {
        if (*px == ' ' || *px == '\n' || *px == '\r') {
            *px = '\0';
            break;
        }
    }

    contr->displayName = strdup(p);
    contr->f_direct = 1;

    fprintf(stderr, "OPEN: displayName=(%s)\n", p);

    cxt = sc_follow_context_new_with_fd(contr->socket_fd, p, 1, BUFSIZE, g_connection);
    g_context_list = az_list_add(g_context_list, cxt);

    set_non_blocking(contr->socket_fd);

    return 0;
}

int
_do_receive_data_0(int c, const void *data, size_t dlen, void* info)
{
    sc_controller* contr = (sc_controller*)info;
    char line[2048], *p, *px;
    size_t u;
    int n, ret = 0;

    az_log(LOG_DEBUG, "data = %p, dlen = %d", data, dlen);

    if (az_buffer_unread_bytes(contr->buffer) == 0) {
        az_buffer_reset(contr->buffer);
    }
    n = az_buffer_fetch_bytes(contr->buffer, data, dlen);

    while ((n = az_buffer_read_line(contr->buffer, line, sizeof(line), &u)) == 0) {
        line[u] = '\0';

        az_log(LOG_DEBUG, "line = [%s]", line);
        if (strncmp(line, "OPEN ", 5) == 0) {
            _do_controller_direct_open_with_line(line, sizeof(line), contr);

            send(c, "OK\r\n", 4, 0);
            ret = 1;
        }
    }

    return ret;
}
int
_do_receive_data(int c, const void *data, size_t dlen, void* info)
{
    sc_controller* contr = (sc_controller*)info;
    char line[2048], *p, *px;
    size_t u;
    int n, ret = 0;

    az_log(LOG_DEBUG, "data = %p, dlen = %d", data, dlen);

    if (az_buffer_unread_bytes(contr->buffer) == 0) {
        az_buffer_reset(contr->buffer);
    }
    n = az_buffer_fetch_bytes(contr->buffer, data, dlen);

    while ((n = az_buffer_read_line(contr->buffer, line, sizeof(line), &u)) == 0) {
        line[u] = '\0';

        if (contr->f_direct) {
            fprintf(stdout, "direct: [%s]\n", line);
        } else {
            az_log(LOG_DEBUG, "line = [%s]", line);
            if (strncmp(line, "OPEN ", 5) == 0) {
                _do_controller_direct_open_with_line(line, sizeof(line), contr);

                send(c, "OK\r\n", 4, 0);
                ret = 1;
            }
        }
    }

    return ret;
}

enum { MAX_EVENTS = 16 };

int
do_receive_0(int c, void* info)
{
    sc_controller* contr = (sc_controller*)info;
    ssize_t n;
    int16_t code = 0;

    az_log(LOG_DEBUG, ">>> do_receive_0");

    unsigned char* databuf = (unsigned char*)malloc(BUFSIZE);
    
    n = recv(c, databuf, BUFSIZE, 0);
    if (n > 0) {
        if (_do_receive_data_0(c, databuf, n, contr) != 0) {
            // close(c);
        }
    } else if (n == 0) {
        az_log(LOG_DEBUG, "connection closed");
        close(c);
    } else {
        az_log(LOG_DEBUG, "recvall error.");
        close(c);
    }

    az_log(LOG_DEBUG, "<<< do_receive_0");

    free(databuf);
    return 0;
}

int
do_receive(int epfd, int c, void* info)
{
    sc_controller* contr = (sc_controller*)info;
    ssize_t n;
    int16_t code = 0;

    az_log(LOG_DEBUG, ">>> do_receive");

    unsigned char* databuf = (unsigned char*)malloc(BUFSIZE);
    
    n = recv(c, databuf, BUFSIZE, 0);
    if (n > 0) {
        if (_do_receive_data(c, databuf, n, contr) != 0) {
            epoll_ctl(epfd, EPOLL_CTL_DEL, c, NULL);
            close(c);

            contr->socket_fd = -1;
            sc_controller_destroy(contr);
        }
    } else if (n == 0) {
        struct epoll_event ev;

        az_log(LOG_DEBUG, "connection closed");

        epoll_ctl(epfd, EPOLL_CTL_DEL, c, NULL);

        close(c);

        contr->socket_fd = -1;
        sc_controller_destroy(contr);
    } else {
        struct epoll_event ev;

        az_log(LOG_DEBUG, "recvall error.");

        epoll_ctl(epfd, EPOLL_CTL_DEL, c, NULL);

        close(c);

        contr->socket_fd = -1;
        sc_controller_destroy(contr);
    }

    az_log(LOG_DEBUG, "<<< do_receive");

    free(databuf);
    return 0;
}


int
setup_epoll(int* socks, int num_socks)
{
    int epfd, i;
    struct epoll_event ev;

    if ((epfd = epoll_create(MAX_EVENTS)) < 0) {
        az_log(LOG_DEBUG, "epoll_create error");
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

    int nfd, c = -1;
    int done = 0;

    // for (;;) {
    {
        int c = -1;

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

                    az_log(LOG_DEBUG, "accepted");

                    contr = sc_controller_new(c);

                    do_receive_0(contr->socket_fd, contr);

/*
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.ptr = contr;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, c, &ev) < 0) {
                        az_log(LOG_DEBUG, "epoll set insertion error: fd = %d", c);
                        continue;
                    }
                    done = 1;
*/
                    break;
                }
            }

/*
            if (!done) {
                contr = events[i].data.ptr;
                do_receive(epfd, contr->socket_fd, contr);
            }
*/
        }
    }

    return nfd > 0 ? 1 : 0;
}

/////

void
usage()
{
    fprintf(stdout, "USAGE: comfollower\n");
}

int
main(int argc, char** argv)
{
    sc_follow_context *cxt = NULL;

    int ret, ch, i, epfd;
    sc_log_message *resp;
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
        az_log(LOG_DEBUG, "error: server address is not assigned.");
        exit(1);
    }

    g_connection = sc_aggregator_connection_new(g_config_server_address, g_config_server_port);
    sc_aggregator_connection_open(g_connection);
    az_log(LOG_DEBUG, "conn = %p", g_connection);

    g_conn_controller = setup_server_unix_socket(PATH_CONTROL);

    do_rotate(g_connection);
    set_rotation_timer();
    set_sigpipe_handler();

    epfd = setup_epoll(&g_conn_controller, 1);

    while (1) {
        az_list* li;
	int sl = 1, rc = 0, cc;
        cxt = NULL;

        if (do_server_socket(epfd, &g_conn_controller, 1) > 0) {
            sl = 0;
        }

	for (li = g_context_list; li; li = li->next) {
            resp = NULL;
            cxt = li->object;
            ret = _run_follow_context(cxt, &resp);
	    if (ret > 0) {
	        if (ret >= 1000) {
		    rc = 1;
		}
	    } else if (ret == -1) {
	        // error occurred
	        perror("_run_follow_context");
	        // exit(1);
                g_context_list = az_list_delete(g_context_list, cxt);
                sc_follow_context_destroy(cxt);
	    } else {
	        // in proceessed any bytes.
	        sl = 0;
	    }

            // here, we proceed response from aggregator
            sc_log_message_destroy(resp);
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
    az_log(LOG_DEBUG, ">>> %s: BEGIN", __FUNCTION__);

    do_rotate(g_connection);

#if 0
    itimer.it_interval.tv_sec = 86400;
#else
    itimer.it_interval.tv_sec = 1;
#endif
    itimer.it_interval.tv_usec = 0;
    itimer.it_value = itimer.it_interval;
    assert(setitimer(ITIMER_REAL, &itimer, 0) == 0);

    az_log(LOG_DEBUG, ">>> %s: END", __FUNCTION__);
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
    az_log(LOG_DEBUG, "secs_left = %lf", secs_left);

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
do_rotate(sc_aggregator_connection_ref conn)
{
    sc_config_pattern_entry *pe;
    struct tm tm;
    time_t t;
    sc_follow_context* cxt = NULL;
    int not_found;
    az_list *li, *lj, *lp;

    time(&t);
    localtime_r(&t, &tm);
    for (lp = g_config_patterns; lp; lp = lp->next) {
        pe = (sc_config_pattern_entry*)lp->object;
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
        az_log(LOG_DEBUG, "fname = [%s] / dispname = [%s]", fn, dn);

        not_found = 1;
        for (li = g_context_list; not_found && li; li = li->next) {
            cxt = li->object;
            if (cxt->filename && strcmp(cxt->filename, fn) == 0) {
		if (strcmp(cxt->displayName, dn) == 0) {
                    // , I'm already following it.
                    not_found = 0;
		} else {
		    // displayName has rotated.
		    sc_follow_context_close(cxt);
		}
                az_log(LOG_DEBUG, "=== already following now.", fn, dn);
                break;
            }
        }

        if (not_found) {
            cxt = sc_follow_context_new(fn, dn, pe->append_timestamp, BUFSIZE, conn);
            g_context_list = az_list_add(g_context_list, cxt);
            az_log(LOG_DEBUG, "added: new follow_context");
        }
    }

#if 0
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
            cxt = sc_follow_context_new_with_fd(contr->socket_fd, dn, 1, BUFSIZE, conn);
            g_context_list = az_list_add(g_context_list, cxt);
            az_log(LOG_DEBUG, "added: new follow_context => displayName: %s", dn);
        }
    }
#endif
}
