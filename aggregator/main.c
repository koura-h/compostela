/* $Id$ */
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <stdio.h>
#include <stddef.h>
#include <netdb.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <libgen.h>
#include <assert.h>
#include <getopt.h>

#include <byteswap.h>

#include "scmessage.h"
#include "supports.h"
#include "azlist.h"

#include "appconfig.h"
#include "config.h"

#include "sclog.h"

enum { MAX_SOCKETS = 10 };
enum { MAX_EVENTS = 10 };

enum { BUFSIZE = 8192 };

int g_config_default_mode = 0644;

const char *DEFAULT_CONF = PATH_SYSCONFDIR "/compostela.conf";

////////////////////////////////////////

typedef struct _sc_channel {
    int id;
    char *filename;
    char *__filename_fullpath;
    //
    struct _sc_connection* connection;
    //
    //struct _sc_channel *_next;
} sc_channel;

////////////////////////////////////////

typedef struct _sc_connection {
    struct sockaddr* sockaddr;
    socklen_t salen;
    //
    char *remote_addr;
    //
    // channel list here
    // struct _sc_channel* channel_list;
    az_list* channel_list;
    //
    int socket;
} sc_connection;

////////////////////////////////////////

char*
__mk_path(const char* parent, const char* fname, char *buf, size_t bufsize)
{
    snprintf(buf, bufsize, "%s/%s", parent, fname);
    return buf;
}

////////////////////////////////////////

sc_channel*
sc_channel_new(const char* fname, sc_connection* conn)
{
    sc_channel* channel = (sc_channel*)malloc(sizeof(sc_channel));
    if (channel) {
        if (fname) {
	    channel->filename = strdup(fname);
	    channel->__filename_fullpath = pathcat(g_config_server_logdir, conn->remote_addr, fname, NULL);
	    sc_log(LOG_DEBUG, "__filename_fullpath = %s", channel->__filename_fullpath);
	}
	channel->connection = conn;
    }
    return channel;
}

void
sc_channel_destroy(sc_channel* channel)
{
    sc_log(LOG_DEBUG, ">>> %s", __func__);
    free(channel->__filename_fullpath);
    free(channel->filename);
    free(channel);
    sc_log(LOG_DEBUG, "<<< %s", __func__);
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
	    sc_log(LOG_DEBUG, "gai_strerror = [%s]", gai_strerror(err));
	    return;
        }
        sc_log(LOG_DEBUG, "accept connection from [%s]", hbuf);
        conn->remote_addr = strdup(hbuf);
    }
}

void
sc_connection_destroy(sc_connection* conn)
{
    sc_channel* c;
    az_list* li;

    sc_log(LOG_DEBUG, ">>> %s", __func__);
    for (li = conn->channel_list; li; li = li->next) {
        sc_channel_destroy(li->object);
    }
    az_list_delete_all(conn->channel_list);

    sc_log(LOG_DEBUG, "<<< %s", __func__);

    free(conn->remote_addr);
    free(conn->sockaddr);
    free(conn);
}

int
sc_connection_register_channel(sc_connection* conn, sc_channel* channel)
{
    int id = 0;
    sc_channel* last = NULL, *c;
    az_list *li;

    for (li = conn->channel_list; li; li = li->next) {
        c = li->object;
        if (channel == c) {
	    // error: double registration
	    return -1;
	}
	id = (id < c->id ? c->id : id);
    }

    conn->channel_list = az_list_add(conn->channel_list, channel);
    channel->id = id + 1;

    return 0;
}

sc_channel*
sc_connection_channel(sc_connection* conn, int id)
{
    az_list* li;
    sc_channel* channel = NULL;
    for (li = conn->channel_list; li; li = li->next) {
        channel = li->object;
        if (channel->id == id) {
	    break;
	}
    }
    return channel;
}

void
sc_connection_delete_channel(sc_connection* conn, sc_channel* channel)
{
    conn->channel_list = az_list_delete(conn->channel_list, channel);
    sc_channel_destroy(channel);
}


////////////////////////////////////////

void
_dump(sc_message* msg)
{
    sc_log(LOG_DEBUG, ">>>DATA");
    fwrite(&msg->content, msg->length, 1, stderr);
    // fflush(stderr);
    sc_log(LOG_DEBUG, "<<<DATA");
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
_do_merge_file(const char* host, const char* path, const char *data, size_t len)
{
    int fd;
    char fullp[PATH_MAX];

    __mk_path(g_config_server_logdir, path, fullp, sizeof(fullp));
    sc_log(LOG_DEBUG, "merging to ... [%s]", fullp);

    fd = open(fullp, O_APPEND | O_RDWR | O_CREAT, g_config_default_mode);
    if (fd == -1) {
        perror("");
	return -1;
    }
    write(fd, host, strlen(host));
    write(fd, " ", 1);
    write(fd, data, len);
    close(fd);

    return 0;
}

int
_do_append_file(const char* path, const char *data, size_t len)
{
    int fd;
    // char path[PATH_MAX], dir[PATH_MAX];
    char dir[PATH_MAX];

    strcpy(dir, path);
    sc_log(LOG_DEBUG, "appending to ... [%s]", path);

    _create_dir(dirname(dir), 0777);

    fd = open(path, O_APPEND | O_RDWR | O_CREAT, g_config_default_mode);
    if (fd == -1) {
        perror("");
	return -1;
    }
    write(fd, data, len);
    close(fd);

    return 0;
}

int
_do_trunc_file(const char* path, off_t pos)
{
    int fd = open(path, O_RDWR);
    if (fd == -1) {
        return -1;
    }

    lseek(fd, pos, SEEK_SET);
    close(fd);

    return 0;
}

/*
int
_do_stat(const char *remote_addr, const char* fname, struct stat *pst)
{
    char path[PATH_MAX];
    __mk_path(remote_addr, fname, path, sizeof(path));

    return stat(path, pst);
}

int
_do_md5(const char* remote_addr, const char* fname, off_t fsize, unsigned char* md5buf)
{
    char path[PATH_MAX];
    __mk_path(remote_addr, fname, path, sizeof(path));

    return __md5_with_size(path, fsize, md5buf);
}
*/

int
handler_init(sc_message* msg, sc_connection* conn)
{
    int n;
    int64_t stlen = 0;
    int32_t slen = msg->length - sizeof(int32_t);

    // char path[PATH_MAX];
    unsigned char *mhash = NULL;
    size_t mhash_size = 0;
    struct stat st;
    char* p;
    int32_t attr = 0;

    sc_message* ok = NULL;
    sc_channel* channel = NULL;

    p = malloc(slen + 1);
    attr = ntohl(*(int32_t*)msg->content);
    memcpy(p, msg->content + sizeof(int32_t), slen);
    p[slen] = '\0';

    channel = sc_channel_new(p, conn);

    // __mk_path(conn->remote_addr, channel->filename, path, sizeof(path));

    if (attr & 0x80000000) {
        memset(&st, 0, sizeof(st));
        stat(channel->__filename_fullpath, &st);
        mhash_with_size(channel->__filename_fullpath, st.st_size, &mhash, &mhash_size);
        stlen = st.st_size;

	dump_mhash(mhash, mhash_size);

        sc_log(LOG_DEBUG, "channel->filename = %s", channel->filename);
        sc_log(LOG_DEBUG, "conn->remote_addr = %s", conn->remote_addr);
    }

    sc_connection_register_channel(conn, channel);

    ok = sc_message_new(sizeof(int32_t) + mhash_size);
    // haha
    ok->code    = htons(SCM_RESP_OK);
    ok->channel = htons(channel->id);
    ok->length  = htonl(sizeof(stlen) + mhash_size);
#if __BYTE_ORDER == __LITTLE_ENDIAN
    stlen = bswap_64(stlen);
#endif
    memcpy(ok->content, &stlen, sizeof(stlen));
    if (mhash) {
        memcpy(ok->content + sizeof(stlen), mhash, mhash_size);
    }
    // haha
    n = sendall(conn->socket, ok, offsetof(sc_message, content) + sizeof(stlen) + mhash_size, 0);

    free(mhash);
    return 0;
}

int
handler_data(sc_message* msg, sc_connection* conn, sc_channel* channel)
{
    int n;
    // char path[PATH_MAX];

    sc_log(LOG_DEBUG, ">>> handler_data");

    // __mk_path(conn->remote_addr, channel->filename, path, sizeof(path));

    sc_message* ok = sc_message_new(sizeof(int32_t));
    sc_log(LOG_DEBUG, "channel_id = %d", msg->channel);
    _do_merge_file(conn->remote_addr, channel->filename, msg->content, msg->length);
    _do_append_file(channel->__filename_fullpath, msg->content, msg->length);

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
handler_rele(sc_message* msg, sc_connection* conn, sc_channel* channel)
{
    int n;
    sc_connection_delete_channel(conn, channel);

    sc_message* ok = sc_message_new(sizeof(int32_t));

    ok->code    = htons(SCM_RESP_OK);
    ok->channel = htons(msg->channel);
    ok->length  = htonl(sizeof(int32_t));
    memset(ok->content, 0, sizeof(int32_t));

    n = sendall(conn->socket, ok, offsetof(sc_message, content) + sizeof(int32_t), 0);

    sc_message_destroy(ok);
    return 0;
}

int
handler_pos(sc_message* msg, sc_connection* conn, sc_channel* channel)
{
    int n;
    // char path[PATH_MAX];
    int64_t pos;

    sc_log(LOG_DEBUG, ">>> handler_pos");

    // __mk_path(conn->remote_addr, channel->filename, path, sizeof(path));

    pos = *(int64_t*)msg->content;
#if __BYTE_ORDER == __LITTLE_ENDIAN
    pos = bswap_64(pos);
#endif

    sc_message* ok = sc_message_new(sizeof(int32_t));
    _do_trunc_file(channel->__filename_fullpath, pos);

    // haha
    ok->code    = htons(SCM_RESP_OK);
    ok->channel = htons(msg->channel);
    ok->length  = htonl(sizeof(int32_t));
    memset(ok->content, 0, sizeof(int32_t));
    // haha
    n = sendall(conn->socket, ok, offsetof(sc_message, content) + sizeof(int32_t), 0);
    sc_message_destroy(ok);

    return 0;
}

int
do_receive(int epfd, sc_connection* conn)
{
    ssize_t n;
    int16_t code = 0;
    sc_message* msg = sc_message_new(BUFSIZE);
    sc_channel* channel = NULL;

    int c = conn->socket;

    n = recvall(c, msg, offsetof(sc_message, content), 0);
    if (n > 0) {
        msg->code    = ntohs(msg->code);
	msg->channel = ntohs(msg->channel);
        msg->length  = ntohl(msg->length);
	sc_log(LOG_DEBUG, "n = %d, code = %d, channel = %d, length = %d", n, msg->code, msg->channel, msg->length);
	if (msg->length > 0) {
            n = recvall(c, &msg->content, msg->length, 0);
	    sc_log(LOG_DEBUG, "n = %d", n, msg->code, msg->channel, msg->length);
	}

	code = msg->code;
	if (msg->channel == 0) {
	    assert(code == SCM_MSG_INIT);
	}

        channel = sc_connection_channel(conn, msg->channel);
	if (!channel) {
	    sc_log(LOG_DEBUG, "conn=%p, I might be happened to restart?", conn);
	    assert(code == SCM_MSG_INIT);
	}

	switch (msg->code) {
	case SCM_MSG_INIT:
	    handler_init(msg, conn);
	    break;
	case SCM_MSG_POS:
	    handler_pos(msg, conn, channel);
	    break;
	case SCM_MSG_DATA:
	    handler_data(msg, conn, channel);
	    break;
	case SCM_MSG_RELE:
	    handler_rele(msg, conn, channel);
	    break;
	}
    } else if (n == 0) {
        struct epoll_event ev;

        sc_log(LOG_DEBUG, "connection closed");

	epoll_ctl(epfd, EPOLL_CTL_DEL, c, NULL);

	close(c);

	sc_connection_destroy(conn);
    } else {
        struct epoll_event ev;

        sc_log(LOG_DEBUG, "recvall error.");

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

    // char buf[2048];
    ssize_t n;
    sc_connection *conn = NULL;

    int done = 0;

    // sc_log(LOG_DEBUG, "num_socks = %d", num_socks);

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
		        sc_log(LOG_DEBUG, "epoll set insertion error: fd = %d", c);
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

    char *conf = NULL, sport[16];
    ssize_t cb, n;

    int yes = 1, ch;

    struct option long_opts[] = {
        { "config", 2, NULL, 0 },
        { "output-dir", 2, NULL, 0 },
	{ "listen-port", 2, NULL, 0 },
	{ "listen-addr", 2, NULL, 0 },
    };

    while ((ch = getopt_long(argc, argv, "c:o:p:L:", long_opts, NULL)) != -1) {
        switch (ch) {
        case 'c':
            conf = strdup(optarg);
            break;
	case 'o':
	    g_config_server_logdir = strdup(optarg);
	    break;
	case 'p':
	    g_config_server_port = strtoul(optarg, NULL, 10);
	    break;
	case 'L':
	    g_config_server_addr = strdup(optarg);
	    break;
	}
    }
    argc -= optind;
    argv += optind;

    load_config_file((conf ? conf : DEFAULT_CONF));
    free(conf);

    //
    set_rotation_timer();
    set_sigpipe_handler();

    //
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    snprintf(sport, sizeof(sport), "%d", g_config_server_port);

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

/////

void
handler_alarm(int sig, siginfo_t* sinfo, void* ptr)
{
    struct itimerval itimer = {};
    sc_log(LOG_DEBUG, ">>> %s: BEGIN", __FUNCTION__);

    // do_rotate(g_connection);

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
