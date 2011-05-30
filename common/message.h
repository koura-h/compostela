/* $Id$ */
#if !defined(__MESSAGE_H__)
#define __MESSAGE_H__


enum {
    SCM_MSG_NONE = 0,
    SCM_MSG_INIT = 100,
    SCM_MSG_DATA = 101,
    SCM_MSG_SEEK = 102,
    SCM_MSG_RELE = 103,
    SCM_MSG_MOVE = 104,
    //
    SCM_RESP_OK = 1001,
};

#define	MAX_DISPLAYNAME_LENGTH	256

typedef struct _sc_message {
    int16_t  code;
    char     displayName[MAX_DISPLAYNAME_LENGTH];
    int32_t  length;
    char    *body;
} sc_message;


#if defined(__cplusplus)
extern "C" {
#endif

sc_message* sc_message_new();
void sc_message_destroy(sc_message* msg);

void* sc_message_pack(sc_message* msg);
int sc_message_unpack(sc_message* msg, const char* data[3]);

#if defined(__cplusplus)
}
#endif


#endif
