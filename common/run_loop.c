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

#include "run_loop.h"


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

void
__run_loop_context_flush(struct __run_loop_context* cxt)
{
    az_list* li;
    for (li = cxt->tasks; li; li = li->next) {
        struct __run_loop_task* t = (struct __run_loop_task*)li->object;
        __run_loop_task_destroy(t);
    }
    az_list_delete_all(cxt->tasks);
    cxt->tasks = NULL;
}


int
__run_loop_context_register_server_socket(struct __run_loop_context* cxt, int* socks, int num_socks)
{
    int i;
    struct epoll_event ev;

    assert(cxt->server_sockets == NULL);
    assert(cxt->server_sockets_count == 0);

    cxt->server_sockets_count = num_socks;
    cxt->server_sockets = malloc(sizeof(int) * num_socks);
    if (!cxt->server_sockets) {
        return -1;
    }

    for (i = 0; i < num_socks; i++) {
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        ev.data.fd = socks[i];
        epoll_ctl(cxt->epoll_fd, EPOLL_CTL_ADD, socks[i], &ev);

        cxt->server_sockets[i] = socks[i];
    }

    return 0;
}


struct __controller_context*
__controller_context_new()
{
    struct __controller_context* cc = (struct __controller_context*)malloc(sizeof(struct __controller_context));
    if (cc) {
        memset(cc, 0, sizeof(struct __controller_context));
        cc->buffer = az_buffer_new(1024);
    }
    return cc;
}

void
__controller_context_destroy(struct __controller_context* cc)
{
    az_buffer_destroy(cc->buffer);
    free(cc);
}

int
__do_receive_line(const char* line, struct __controller_context* cc, struct __run_loop_context *cxt)
{
    az_log(LOG_DEBUG, "line = [%s]", line);
    return 0;
}

int
__do_receive(int fd, struct __controller_context* cc, struct __run_loop_context* cxt)
{
    char buf[1024];
    size_t used;
    int err, ret, n = 1; // dummy

    az_log(LOG_DEBUG, "__do_receive");

    while (n > 0) {
        while ((ret = az_buffer_read_line(cc->buffer, buf, sizeof(buf), &used, &err)) != 1) {
            n = az_buffer_fetch_file(cc->buffer, fd, 1024);
            if (n == -1) {
                if (errno == EAGAIN) {
                    az_buffer_push_back(cc->buffer, buf, used);
                    return 0; // continue.
                } else {
                    buf[used] = '\0';
                    __do_receive_line(buf, cc, cxt);
                    return -1; // error, stop.
                }
            } else if (n == 0) {
                buf[used] = '\0';
                __do_receive_line(buf, cc, cxt);
                return -1; // EOF
            }
            az_log(LOG_DEBUG, "n = %d\n", n);
        }

        buf[used] = '\0';
        __do_receive_line(buf, cc, cxt);
    }

    return -1;
}

enum { MAX_EVENTS = 10 };

int
__run_loop_context_wait(struct __run_loop_context* cxt, int timeout)
{
    struct epoll_event events[MAX_EVENTS], ev;
    int i, j, nfd, done = 0, c = -1, to = timeout;
    int* socks = cxt->server_sockets;
    int num_socks = cxt->server_sockets_count;

    struct __controller_context *cc;

    if (cxt->tasks) {
        to = 0;
    }

    nfd = epoll_wait(cxt->epoll_fd, events, MAX_EVENTS, to);
    for (i = 0; i < nfd; i++) {
        done = 0;
        for (j = 0; j < num_socks; j++) {
            if (events[i].data.fd == socks[j]) {
                struct sockaddr_storage ss;
                socklen_t sslen = sizeof(ss);

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
                cc = __controller_context_new(1024);
                cc->fd = c;

                if (set_non_blocking(c) == -1) {
                    return -1;
                }

                memset(&ev, 0, sizeof(ev));
                ev.events = EPOLLIN | EPOLLET;
                ev.data.ptr = cc;
                if (epoll_ctl(cxt->epoll_fd, EPOLL_CTL_ADD, c, &ev) < 0) {
                    az_log(LOG_DEBUG, "epoll set insertion error: fd = %d", c);
                    continue;
                }
                done = 1;
                break;
            }
        }

        if (!done) {
            cc = events[i].data.ptr;
            az_log(LOG_DEBUG, "fd = %d", cc->fd);
            if (__do_receive(cc->fd, cc, cxt) < 0) {
                if (epoll_ctl(cxt->epoll_fd, EPOLL_CTL_DEL, cc->fd, NULL) < 0) {
                    az_log(LOG_DEBUG, "epoll error: fd=%d: %s", cc->fd, strerror(errno));
                }
                az_log(LOG_DEBUG, "fd=%d: closed", cc->fd);
                close(cc->fd);
                __controller_context_destroy(cc);
            }
        }
    }

    az_log(LOG_DEBUG, "nfd = %d", nfd);
    return nfd > 0 ? 1 : 0;
}


struct __run_loop_context*
__run_loop_context_new(int c)
{
    struct __run_loop_context* cxt = NULL;

    cxt = malloc(sizeof(struct __run_loop_context));
    if (cxt) {
        memset(cxt, 0, sizeof(struct __run_loop_context));

        if ((cxt->epoll_fd = epoll_create(MAX_EVENTS)) < 0) {
            az_log(LOG_DEBUG, "epoll_create error");
            free(cxt);
            return NULL;
        }
    }

    return cxt;
}

