/* $Id$ */
#if !defined(__COMMON_H__)
#define __COMMON_H__


typedef struct _sc_message {
    int16_t command;
    int16_t channel_code;
    off_t   position;
    int32_t length;
    char    content;
} sc_message;

#if defined(__cplusplus)
extern "C" {
#endif

sc_message*
sc_message_new(ssize_t content_size);

void
sc_message_destroy(sc_message* msg);

#if defined(__cplusplus)
}
#endif


#endif
