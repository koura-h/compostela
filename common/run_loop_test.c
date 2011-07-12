#include <stdlib.h>
#include <stdio.h>
#include <glob.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "azbuffer.h"
#include "azlog.h"
#include "azlist.h"
#include "supports.h"


#include "run_loop.h"

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
__diff_filewatch(az_list* list_orig, az_list* list, struct __run_loop_context* cxt)
{
    az_list* li;
    struct __run_loop_task* t;

    for (li = list_orig; li; li = li->next) {
        struct __filewatch* wf = (struct __filewatch*)li->object;
        if (wf->__generation != 2) {
            t = __run_loop_task_new();
            t->class = TASK_FILE_DELETED;
            t->object = wf;
            cxt->tasks = az_list_add(cxt->tasks, t);
        }
    }

    for (li = list; li; li = li->next) {
        struct __filewatch* wf = (struct __filewatch*)li->object;
        if (wf->__generation == 0) {
            t = __run_loop_task_new();
            t->class = TASK_FILE_ADDED;
            t->object = wf;
            cxt->tasks = az_list_add(cxt->tasks, t);
        } else if (wf->__generation == 1) {
            // printf("NOT MODIFIED: %s\n", wf->filename);
        } else if (wf->__generation == 3) {
            t = __run_loop_task_new();
            t->class = TASK_FILE_MODIFIED;
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
__exec_tasks(struct __run_loop_context* cxt)
{
    az_list* li;

    for (li = cxt->tasks; li; li = li->next) {
        struct __run_loop_task* t = (struct __run_loop_task*)li->object;
        struct __filewatch* wf = (struct __filewatch*)t->object;
        switch (t->class) {
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
main(int argc, char** argv)
{
    struct __run_loop_context *cxt;
    az_list *wfiles = NULL, *wfiles0 = NULL;
    int ss;

    cxt = __run_loop_context_new(1);
    ss = __setup_control_path(argv[1]);
    __run_loop_context_register_server_socket(cxt, &ss, 1);

    // wfiles0 = __make_filewatch_with_glob(argv[2]);

    while (1) {
        assert(cxt->tasks == NULL);

        wfiles = __make_filewatch_with_glob(argv[2]);
        __eval_filewatch(wfiles0, wfiles);
        __diff_filewatch(wfiles0, wfiles, cxt);

        __run_loop_context_wait(cxt, 1000);

        // execution
        __exec_tasks(cxt);

        // for next step
        __run_loop_context_flush(cxt);
       
        __filewatch_destroy(wfiles0);

        wfiles0 = wfiles;
        wfiles = NULL;
    }
}

