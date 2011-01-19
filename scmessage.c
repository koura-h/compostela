/* $Id$ */
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

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

