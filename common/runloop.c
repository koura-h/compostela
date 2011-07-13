#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

#include "azbuffer.h"
#include "azlog.h"
#include "azlist.h"
#include "supports.h"

#include "runloop.h"


struct __run_loop_task* 
__run_loop_task_new()
{
    struct __run_loop_task *t = (struct __run_loop_task*)malloc(sizeof(struct __run_loop_task));
    if (t) {
        memset(t, 0, sizeof(struct __run_loop_task));
    }

    return t;
}

void
__run_loop_task_destroy(struct __run_loop_task* t)
{
    free(t);
}

//////////

void
__run_loop_flush(struct __run_loop* loop)
{
    az_list* li;
    for (li = loop->tasks; li; li = li->next) {
        struct __run_loop_task* t = (struct __run_loop_task*)li->object;
        __run_loop_task_destroy(t);
    }
    az_list_delete_all(loop->tasks);
    loop->tasks = NULL;
}


struct __run_loop_epoll_event_data {
    int _fd;
    int _is_server;
    void *_data;
};

int
__run_loop_register_server_socket(struct __run_loop* loop, int* socks, int num_socks)
{
    int i;
    struct epoll_event ev;
    struct __run_loop_epoll_event_data *data;

    for (i = 0; i < num_socks; i++) {
        data = malloc(sizeof(struct __run_loop_epoll_event_data));
        data->_is_server = 1;
        data->_fd = socks[i];
        data->_data = NULL;

        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        // ev.data.fd = socks[i];
        ev.data.ptr = data;
        epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, socks[i], &ev);
    }

    return 0;
}

enum { MAX_EVENTS = 10 };

int
__run_loop_wait(struct __run_loop* loop, int timeout, __receive_func recvfunc)
{
    struct epoll_event events[MAX_EVENTS], ev;
    int i, nfd, c = -1, to = timeout;

    struct __connection *conn;

    if (loop->tasks) {
        to = 0;
    }

    nfd = epoll_wait(loop->epoll_fd, events, MAX_EVENTS, to);
    for (i = 0; i < nfd; i++) {
        struct __run_loop_epoll_event_data* data = events[i].data.ptr, *d = NULL;

        if (data->_is_server) {
            struct sockaddr_storage ss;
            socklen_t sslen = sizeof(ss);
            memset(&ss, 0, sizeof(ss));

            c = accept(data->_fd, (struct sockaddr*)&ss, &sslen);
            if (c < 0) {
                if (errno == EAGAIN) {
                    continue;
                } else {
                    perror("accept");
                }
            }

            az_log(LOG_DEBUG, "accepted");
            conn = __connection_new(1024);
            conn->fd = c;
            conn->receive_func = recvfunc;

            if (set_non_blocking(c) == -1) {
                return -1;
            }

            d = malloc(sizeof(struct __run_loop_epoll_event_data));
            d->_is_server = 0;
            d->_fd = c;
            d->_data = conn;

            memset(&ev, 0, sizeof(ev));
            ev.events = EPOLLIN | EPOLLET;
            ev.data.ptr = d;
            if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, c, &ev) < 0) {
                az_log(LOG_DEBUG, "epoll set insertion error: fd = %d", c);
                continue;
            }
        } else {
            int ret = -1;

            conn = data->_data;
            assert(conn->fd == data->_fd);
            az_log(LOG_DEBUG, "fd = %d", data->_fd);
            // if (__do_receive(conn, loop) < 0) {
            if (conn->receive_func) {
               ret = conn->receive_func(conn, loop);
            }
            if (ret < 0) {
                if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, data->_fd, NULL) < 0) {
                    az_log(LOG_DEBUG, "epoll error: fd=%d: %s", data->_fd, strerror(errno));
                }
                az_log(LOG_DEBUG, "fd = %d: closed", data->_fd);
                close(data->_fd);
                __connection_destroy(conn);
                free(data);
            }
        }
    }

    az_log(LOG_DEBUG, "nfd = %d", nfd);
    return nfd > 0 ? 1 : 0;
}


struct __run_loop*
__run_loop_new(int c)
{
    struct __run_loop* loop = NULL;

    loop = malloc(sizeof(struct __run_loop));
    if (loop) {
        memset(loop, 0, sizeof(struct __run_loop));

        if ((loop->epoll_fd = epoll_create(MAX_EVENTS)) < 0) {
            az_log(LOG_DEBUG, "epoll_create error");
            free(loop);
            return NULL;
        }
    }

    return loop;
}


//////////

struct __connection*
__connection_new()
{
    struct __connection* conn = (struct __connection*)malloc(sizeof(struct __connection));
    if (conn) {
        memset(conn, 0, sizeof(struct __connection));
        conn->buffer = az_buffer_new(1024);
    }
    return conn;
}

void
__connection_destroy(struct __connection* conn)
{
    az_buffer_destroy(conn->buffer);
    free(conn);
}

