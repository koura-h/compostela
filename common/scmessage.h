/* $Id$ */
#if !defined(__COMMON_H__)
#define __COMMON_H__


enum {
    SCM_MSG_NONE = 0,
    SCM_MSG_INIT = 100,
    SCM_MSG_DATA = 101,
    SCM_MSG_POS  = 102,
    SCM_MSG_RELE = 103,
    SCM_MSG_MOVE = 104,
    //
    SCM_RESP_OK = 1001,
};

typedef struct _sc_message_0 {
    int16_t code;
    int16_t channel;
    int32_t length;
    char    content[1];
} sc_message_0;


typedef struct _scm_response_init {
} scm_response_init;

typedef struct _scm_command_pos {
    int64_t filesize;
} scm_command_pos;

#if defined(__cplusplus)
extern "C" {
#endif

sc_message_0* sc_message_0_new(ssize_t content_size);

void sc_message_0_destroy(sc_message_0* msg);

sc_message_0* sc_message_0_resize(sc_message_0* msg, ssize_t newsize);

#if defined(__cplusplus)
}
#endif


#endif
