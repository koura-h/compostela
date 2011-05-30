/* $Id$ */
#include <sys/types.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "scmessage.h"

////////////////////

sc_message_0*
sc_message_0_new(ssize_t content_size)
{
    sc_message_0 *msg = (sc_message_0*)malloc(offsetof(sc_message_0, content) + content_size);
    return msg;
}

void
sc_message_0_destroy(sc_message_0* msg)
{
    free(msg);
}

sc_message_0*
sc_message_0_resize(sc_message_0* msg, ssize_t newsize)
{
    sc_message_0 *ret = (sc_message_0*)realloc(msg, offsetof(sc_message_0, content) + newsize);
    return ret;
}
