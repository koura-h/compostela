#include <stdlib.h>
#include <stdio.h>
#include <glob.h>
#include <string.h>
#include <getopt.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <byteswap.h>
#include <libgen.h>

#include "azbuffer.h"
#include "azlog.h"
#include "azlist.h"
#include "supports.h"

#include "runloop.h"

#include "message.h"
#include "connection.h"
#include "follow_context.h"
#include "appconfig.h"

#include "config.h"



enum { ERR_MUST_RECONNECT = 1001 };

// enum { CONTROLLER_BUFFER_SIZE = 2048 };

enum { BUFSIZE = 8192 };

az_list* g_context_list = NULL;

const char *DEFAULT_CONF = PATH_SYSCONFDIR "/comfollower.conf";
#define PATH_CONTROL "/tmp/comfollower.sock"

static int do_rotate(sc_aggregator_connection_ref conn);

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


/*
 * 1) read ok, line completed.
 * 2) read ok, line not completed. (LF not found)
 * 3) readable but no data available, wait.
 * 4) read error.
 *
 *
 */
int
_sc_follow_context_read_line(sc_follow_context* cxt, char* dst, size_t dsize, size_t* used)
{
    int n, err;
    char* p = dst;

    *used = 0;

    az_log(LOG_DEBUG, ">>> _sc_follow_context_read_line");

    if (az_buffer_unread_bytes(cxt->buffer) == 0) {
        az_buffer_reset(cxt->buffer);
        n = az_buffer_fetch_file(cxt->buffer, cxt->_fd, az_buffer_unused_bytes(cxt->buffer));
        if (n <= 0) {
            if (errno == EAGAIN) { // for read()
                return n;
            }
            return n;
        }
    }

    err = 0;
    return az_buffer_read_line(cxt->buffer, p, dst + dsize - p, used, &err);
#if 0
    while ((n = az_buffer_read_line(cxt->buffer, p, dst + dsize - p, &u, &err)) != 1) {
        assert(n == 0); // Now, 'dst/dsize' assumes to have enough space always.

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
#endif
}



//////////

int
dump(struct stat *st)
{
    char buf[256];

    printf("File type:                ");

    switch (st->st_mode & S_IFMT) {
        case S_IFBLK:  printf("block device\n");            break;
        case S_IFCHR:  printf("character device\n");        break;
        case S_IFDIR:  printf("directory\n");               break;
        case S_IFIFO:  printf("FIFO/pipe\n");               break;
        case S_IFLNK:  printf("symlink\n");                 break;
        case S_IFREG:  printf("regular file\n");            break;
        case S_IFSOCK: printf("socket\n");                  break;
        default:       printf("unknown?\n");                break;
    }

    printf("I-node number:            %ld\n", (long) st->st_ino);

    printf("Mode:                     %lo (octal)\n",
           (unsigned long) st->st_mode);

    printf("Link count:               %ld\n", (long) st->st_nlink);
    printf("Ownership:                UID=%ld   GID=%ld\n",
           (long) st->st_uid, (long) st->st_gid);

    printf("Preferred I/O block size: %ld bytes\n",
           (long) st->st_blksize);
    printf("File size:                %lld bytes\n",
           (long long) st->st_size);
    printf("Blocks allocated:         %lld\n",
           (long long) st->st_blocks);

    ctime_r(&(st->st_ctime), buf);
    printf("Last inode change:        %s", buf);
    ctime_r(&(st->st_atime), buf);
    printf("Last file access:         %s", buf);
    ctime_r(&(st->st_mtime), buf);
    printf("Last file modification:   %s", buf);

    return 0;
}

struct __filewatch {
    char *filename;
    struct stat st;
    int __generation;
};


struct __filewatch*
__find_entry_with_filename(az_list* list, const char* fname)
{
    az_list* li;
    for (li = list; li; li = li->next) {
        struct __filewatch *wf = (struct __filewatch*)li->object;
        if (strcmp(fname, wf->filename) == 0) {
            return wf;
        }
    }
    return NULL;
}

int
__eval_filewatch(az_list* list_orig, az_list* list)
{
    az_list *li;

    if (!list_orig) {
        return -1;
    }

    for (li = list; li; li = li->next) {
        struct __filewatch *wf, *wf0 = NULL;
        wf = (struct __filewatch*)li->object;
        wf0 = __find_entry_with_filename(list_orig, wf->filename);
        if (wf0) {
            // modified or not
            wf0->__generation = 2;
            if (wf0->st.st_mtime != wf->st.st_mtime) {
                // modified
                wf->__generation = 3;
            } else {
                // not modified
                wf->__generation = 1;
            }
        } else {
            // added
            wf->__generation = 0;
        }
    }

    return 0;
}

int
__diff_filewatch(az_list* list_orig, az_list* list, struct __run_loop* cxt)
{
    az_list* li;
    struct __run_loop_task* t;

    for (li = list_orig; li; li = li->next) {
        struct __filewatch* wf = (struct __filewatch*)li->object;
        if (wf->__generation != 2) {
            t = __run_loop_task_new();
            t->type = TASK_FILE_DELETED;
            t->object = wf;
            cxt->tasks = az_list_add(cxt->tasks, t);
        }
    }

    for (li = list; li; li = li->next) {
        struct __filewatch* wf = (struct __filewatch*)li->object;
        if (wf->__generation == 0) {
            t = __run_loop_task_new();
            t->type = TASK_FILE_ADDED;
            t->object = wf;
            cxt->tasks = az_list_add(cxt->tasks, t);
        } else if (wf->__generation == 1) {
            // printf("NOT MODIFIED: %s\n", wf->filename);
        } else if (wf->__generation == 3) {
            t = __run_loop_task_new();
            t->type = TASK_FILE_MODIFIED;
            t->object = wf;
            cxt->tasks = az_list_add(cxt->tasks, t);
        }
    }

    return 0;
}

int
__setup_control_path(const char* path)
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

void
showdir(az_list* wflist)
{
    az_list *li;
    for (li = wflist; li; li = li->next) {
        struct __filewatch *wf = (struct __filewatch*)li->object;
        printf(">>> %s\n", wf->filename);
    }
}

void
__filewatch_destroy(az_list* wflist)
{
    az_list *li;
    for (li = wflist; li; li = li->next) {
        struct __filewatch *wf = (struct __filewatch*)li->object;
        free(wf->filename);
        free(wf);
    }

    az_list_delete_all(wflist);
}

az_list*
__make_filewatch_with_glob(const char* path)
{
    az_list* files = NULL;

    glob_t globbuf;
    int i;

    glob(path, 0, NULL, &globbuf);

    for (i = 0; i < globbuf.gl_pathc; i++) {
        struct __filewatch *wf = malloc(sizeof(struct __filewatch));
        memset(wf, 0, sizeof(struct __filewatch));

        wf->filename = strdup(globbuf.gl_pathv[i]);
        stat(wf->filename, &(wf->st));

        files = az_list_add(files, wf);
    }
    globfree(&globbuf);

    return files;
}

int
__exec_tasks(struct __run_loop* cxt)
{
    az_list* li;

    for (li = cxt->tasks; li; li = li->next) {
        struct __run_loop_task* t = (struct __run_loop_task*)li->object;
        struct __filewatch* wf = (struct __filewatch*)t->object;
        switch (t->type) {
            case TASK_FILE_ADDED:
                printf("ADDED: %s\n", wf->filename);
                break;
            case TASK_FILE_MODIFIED:
                printf("MODIFIED: %s\n", wf->filename);
                break;
            case TASK_FILE_DELETED:
                printf("DELETED: %s\n", wf->filename);
                break;
        }
    }

    return 0;
}



int
__do_receive_1(const char* line, struct __connection* conn, struct __run_loop *loop)
{
    az_log(LOG_DEBUG, "line = [%s]", line);
    return 0;
}

int
__do_receive(struct __connection* conn, struct __run_loop* loop)
{
    char buf[1024];
    size_t used;
    int err, ret, n = 1; // dummy

    az_log(LOG_DEBUG, "__do_receive");

    while (n > 0) {
        while ((ret = az_buffer_read_line(conn->buffer, buf, sizeof(buf), &used, &err)) != 1) {
            n = az_buffer_fetch_file(conn->buffer, conn->fd, 1024);
            if (n == -1) {
                if (errno == EAGAIN) {
                    az_buffer_push_back(conn->buffer, buf, used);
                    return 0; // continue.
                } else {
                    buf[used] = '\0';
                    __do_receive_1(buf, conn, loop);
                    return -1; // error, stop.
                }
            } else if (n == 0) {
                buf[used] = '\0';
                __do_receive_1(buf, conn, loop);
                return -1; // EOF
            }
            az_log(LOG_DEBUG, "n = %d\n", n);
        }

        buf[used] = '\0';
        __do_receive_1(buf, conn, loop);
    }

    return -1;
}

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
        unsigned char *buf;
        char *p;
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
_run_follow_context(sc_follow_context* cxt, sc_log_message** presp)
{
    // sc_log_message* msg = sc_log_message_new(csize), *resp = NULL;
    int ret = 0;
    time_t t;
    int32_t attr = 0;
    size_t cb = 0, cb0 = sizeof(int32_t) + sizeof(int64_t);

    assert(presp != NULL);
    *presp = NULL;

    sc_log_message* msgbuf = cxt->message_buffer;

    az_log(LOG_DEBUG, "context run");
    if (!sc_aggregator_connection_is_opened(cxt->connection)) {
        // disconnected. but show must go on.
        az_log(LOG_DEBUG, ">>> %s: PLEASE RECONNECT NOW!", __FUNCTION__);
        return ERR_MUST_RECONNECT;
    }

    if (msgbuf->code == SCM_MSG_NONE) {
        if (cxt->_fd <= 0) {
            if (sc_follow_context_open_file(cxt) != 0) {
                az_log(LOG_DEBUG, "sc_follow_context_run: not opened yet => [%s]", cxt->filename);
                return 1;
            }
            _init_file(cxt);
        }

        time(&t);

        cb = 0;
        ret = _sc_follow_context_read_line(cxt, msgbuf->content + cb0, BUFSIZE - cb0, &cb);
        if (ret == 0 && cb == 0) {
            // EOF, wait for the new available data.
            return 1;
        } else if (ret == -1) {
            if (errno == EAGAIN) {
                // treats similar to EOF
                return 1;
            }
            return -1;
        }

        attr |= (ret ? 0x80000000 : 0); // line completed?

        attr = htonl(attr);
#if __BYTE_ORDER == __LITTLE_ENDIAN
        t = bswap_64(t);
#endif

        memcpy(msgbuf->content, &attr, sizeof(int32_t));
        memcpy(msgbuf->content + sizeof(int32_t), &t, sizeof(time_t));

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
        return ERR_MUST_RECONNECT;
    }

    if ((*presp)->code == SCM_RESP_OK) {
        // cxt->current_position = cur;
        msgbuf->code = SCM_MSG_NONE;
    }

    return 0;
}


static sc_aggregator_connection_ref g_connection = NULL;



void
usage()
{
    fprintf(stdout, "USAGE: comfollower\n");
}

int
main(int argc, char** argv)
{
    struct __run_loop *loop;
#if USE_GLOB
    az_list *wfiles = NULL, *wfiles0 = NULL;
#endif

    sc_follow_context *cxt = NULL;

    int ret, ch, sl = 0;
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

#if 0
    g_conn_controller = setup_server_unix_socket(PATH_CONTROL);
#else
    g_conn_controller = setup_server_unix_socket(g_config_control_path ? g_config_control_path : PATH_CONTROL);
#endif

    do_rotate(g_connection);
    set_sigpipe_handler();

    loop = __run_loop_new(1);
    __run_loop_register_server_socket(loop, &g_conn_controller, 1);

    // wfiles0 = __make_filewatch_with_glob(argv[2]);

    while (1) {
        assert(loop->tasks == NULL);

#if USE_GLOB
        wfiles = __make_filewatch_with_glob(argv[2]);
        __eval_filewatch(wfiles0, wfiles);
        __diff_filewatch(wfiles0, wfiles, cxt);
#else
        do_rotate(g_connection);
#endif
        __run_loop_wait(loop, 1000 * sl, __do_receive);

        // execution
        __exec_tasks(loop);

        // for next step
        __run_loop_flush(loop);
       
#if USE_GLOB
        __filewatch_destroy(wfiles0);

        wfiles0 = wfiles;
        wfiles = NULL;
#else
        az_list* li;
        int rc = 0;
        cxt = NULL;

        sl = 1;
        for (li = g_context_list; li; li = li->next) {
            resp = NULL;
            cxt = li->object;
            ret = _run_follow_context(cxt, &resp);
            if (ret > 0) {
                if (ret == ERR_MUST_RECONNECT) {
                    rc = 1;
                }
            } else if (ret == -1) {
                // error occurred
                perror("_run_follow_context");
                az_log(LOG_DEBUG, "cxt = %p, cxt->_fd = %d, cxt->displayName = %s", cxt, cxt->_fd, cxt->displayName);
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
#endif
    }
}


int
do_rotate(sc_aggregator_connection_ref conn)
{
    sc_config_pattern_entry *pe;
    struct tm tm;
    time_t t;
    sc_follow_context* cxt = NULL;
    int not_found;
    az_list *li, *lp;

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

    return 0;
}
