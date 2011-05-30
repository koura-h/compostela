/* $Id$ */
#if !defined(__FOLLOW_CONTEXT_H__)
#define __FOLLOW_CONTEXT_H__

typedef struct _sc_follow_context {
    char *filename;
    int channel;
    // off_t current_position;
    off_t filesize;
    mode_t mode;
    int _fd;
    //
    az_buffer_ref buffer;
    //
    sc_message_0* message_buffer;
    //
    char *displayName;
    //
    int ftimestamp;
    //
    sc_aggregator_connection_ref connection;
} sc_follow_context;

/////

#if defined(__cplusplus)
extern "C" {
#endif

int sc_follow_context_sync_file(sc_follow_context *cxt);

void sc_follow_context_destroy(sc_follow_context* cxt);

sc_follow_context* sc_follow_context_new(const char* fname, const char* dispname, int ftimestamp, size_t bufsize, sc_aggregator_connection_ref conn);

sc_follow_context* sc_follow_context_new_with_fd(int fd, const char* dispname, int ftimestamp, size_t bufsize, sc_aggregator_connection_ref conn);

int sc_follow_context_open_file(sc_follow_context* cxt);

int sc_follow_context_close_file(sc_follow_context* cxt);

void sc_follow_context_reset(sc_follow_context* cxt);

int sc_follow_context_close(sc_follow_context* cxt);

void sc_follow_context_destroy(sc_follow_context* cxt);

#if defined(__cplusplus)
}
#endif

#endif
