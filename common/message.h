/* $Id$ */
#if !defined(__MESSAGE_H__)
#define __MESSAGE_H__


enum {
    SCM_MSG_NONE = 0,
    SCM_MSG_SYNC = 100,
    SCM_MSG_DATA = 101,
    SCM_MSG_SEEK  = 102,
    SCM_MSG_RELE = 103,
    SCM_MSG_MOVE = 104,
    //
    SCM_RESP_OK = 1001,
};

typedef struct _sc_log_message {
    int16_t code;
    int16_t channel;
    int32_t length;
    char    content[1];
} sc_log_message;


typedef struct _scm_response_init {
} scm_response_init;

typedef struct _scm_command_pos {
    int64_t filesize;
} scm_command_pos;

#if defined(__cplusplus)
extern "C" {
#endif

sc_log_message* sc_log_message_new(ssize_t content_size);

void sc_log_message_destroy(sc_log_message* msg);

sc_log_message* sc_log_message_resize(sc_log_message* msg, ssize_t newsize);

#if defined(__cplusplus)
}
#endif


#endif
