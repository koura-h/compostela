#if !defined(__RUN_LOOP_H__)
#define __RUN_LOOP_H__


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
    int class;
    void *object;
};

struct __run_loop_task* __run_loop_task_new();

void __run_loop_task_destroy(struct __run_loop_task* t);

struct __run_loop_context {
    int epoll_fd;
    int *server_sockets;
    int server_sockets_count;
    az_list* tasks;
};

void __run_loop_context_flush(struct __run_loop_context* cxt);

struct __run_loop_context* __run_loop_context_new(int c);

int __run_loop_context_register_server_socket(struct __run_loop_context* cxt, int* socks, int num_socks);
int __run_loop_context_wait(struct __run_loop_context* cxt, int timeout);


struct __controller_context {
    int fd;
    az_buffer_ref buffer;
};

struct __controller_context* __controller_context_new();

void __controller_context_destroy(struct __controller_context* cc);

#if defined(__cplusplus)
}
#endif

#endif
