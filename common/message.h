/* $Id$ */
#if !defined(__MESSAGE_H__)
#define __MESSAGE_H__


enum {
    SCM_MSG_NONE = 0,
    SCM_MSG_INIT = 101,
    SCM_MSG_RSET,
    SCM_MSG_SYNC,
    SCM_MSG_DATA,
    SCM_MSG_SEEK,
    SCM_MSG_RELE,
    SCM_MSG_MOVE,
    SCM_MSG_PURG,
    //
    SCM_RESP_OK = 1001,
    SCM_RESP_NG = 1999,
};

typedef struct _sc_log_message {
    int16_t code;
    int16_t channel;
    // int32_t length;
    int32_t content_length;
    char    content[1];
} sc_log_message;


typedef struct _scm_response_init {
} scm_response_init;

typedef struct _scm_command_pos {
    int64_t filesize;
} scm_command_pos;

typedef struct _scm_data_header {
    int32_t attributes;
    int64_t position;
    int64_t timestamp;
    int64_t length;
    //
    char* text;
} scm_data_header;

typedef struct _scm_init_header {
    int32_t attributes;
    //
    char* text;
} scm_init_header;

#if defined(__cplusplus)
extern "C" {
#endif

sc_log_message* sc_log_message_new(ssize_t content_size);

void sc_log_message_destroy(sc_log_message* msg);

sc_log_message* sc_log_message_resize(sc_log_message* msg, ssize_t newsize);


/////

int _unpack_init_header(sc_log_message* msg, scm_init_header* hdr);
int _unpack_data_header(sc_log_message* msg, scm_data_header* data);


#if defined(__cplusplus)
}
#endif


#endif
