/* $Id$ */
#include <sys/types.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <syslog.h>
#include <byteswap.h>
#include "azlog.h"

#include "message.h"

////////////////////

sc_log_message*
sc_log_message_new(ssize_t content_size)
{
    size_t n = offsetof(sc_log_message, content) + content_size;
    sc_log_message *msg = (sc_log_message*)malloc(n);
    if (msg) {
        msg->content_length = content_size;
    }
    return msg;
}

void
sc_log_message_destroy(sc_log_message* msg)
{
    free(msg);
}

sc_log_message*
sc_log_message_resize(sc_log_message* msg, ssize_t newsize)
{
    sc_log_message *ret = (sc_log_message*)realloc(msg, offsetof(sc_log_message, content) + newsize);
    if (ret) {
        msg->content_length = newsize;
    }
    return ret;
}


/////

int
_unpack_data_header(sc_log_message* msg, scm_data_header* data)
{
    int32_t attr;
    time_t t;
    size_t pos;
    size_t len;

    attr = ntohl(*(int32_t*)msg->content);
    t = *(time_t*)(msg->content + sizeof(int32_t));
    pos = *(size_t*)(msg->content + sizeof(int32_t) + sizeof(time_t));
#if __BYTE_ORDER == __LITTLE_ENDIAN
    t = bswap_64(t);
    pos = bswap_64(pos);
#endif
    len = msg->content_length - (sizeof(int32_t) + sizeof(time_t) + sizeof(size_t));
    az_log(LOG_DEBUG, ">>> pos = %d, attr = %lx, len = %d", pos, attr, len);

    data->attributes = attr;
    data->position = pos;
    data->text = msg->content + sizeof(int32_t) + sizeof(time_t) + sizeof(size_t);
    data->length = len;

    return 0;
}


int
_unpack_init_header(sc_log_message *msg, scm_init_header *hdr)
{
    char* p;
    int32_t attr;
    int32_t slen = msg->content_length - sizeof(int32_t);

    p = malloc(slen + 1);
    attr = ntohl(*(int32_t*)msg->content);
    memcpy(p, msg->content + sizeof(int32_t), slen);
    p[slen] = '\0';

    hdr->attributes = attr;
    hdr->text = p;

    return 0;
}

