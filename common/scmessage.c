/* $Id$ */
#include <sys/types.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "scmessage.h"

////////////////////

sc_message*
sc_message_new(ssize_t content_size)
{
    sc_message *msg = (sc_message*)malloc(offsetof(sc_message, content) + content_size);
    return msg;
}

void
sc_message_destroy(sc_message* msg)
{
    free(msg);
}

sc_message*
sc_message_resize(sc_message* msg, ssize_t newsize)
{
    sc_message *ret = (sc_message*)realloc(msg, offsetof(sc_message, content) + newsize);
    return ret;
}
