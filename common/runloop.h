#if !defined(__RUNLOOP_H__)
#define __RUNLOOP_H__


#if defined(__cplusplus)
extern "C" {
#endif

enum {
    TASK_NONE,
    TASK_FILE_ADDED,
    TASK_FILE_MODIFIED,
    TASK_FILE_DELETED,
    //
    TASK_CONTROL_NEW,
};

struct __run_loop_task {
    int type;
    void *object;
};

struct __run_loop {
    int epoll_fd;
    az_list* tasks;
};

typedef struct __connection {
    int fd;
    az_buffer_ref buffer;
    //
    int (*receive_func)(struct __connection*, struct __run_loop*);
} az_connection; 

typedef int (*__receive_func)(struct __connection*, struct __run_loop*);

struct __connection* __connection_new();

void __connection_destroy(struct __connection* conn);

/////

struct __run_loop_task* __run_loop_task_new();
void __run_loop_task_destroy(struct __run_loop_task* t);

struct __run_loop* __run_loop_new(int c);

void __run_loop_flush(struct __run_loop* cxt);
int __run_loop_register_server_socket(struct __run_loop* cxt, int* socks, int num_socks);
int __run_loop_wait(struct __run_loop* cxt, int timeout, __receive_func recvfunc);


#if defined(__cplusplus)
}
#endif

#endif
