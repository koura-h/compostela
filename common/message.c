/* $Id$ */
#include <sys/types.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "message.h"

#include "azlog.h"

////////////////////
static char*
_c2s(int c)
{
    switch (c) {
    case SCM_MSG_INIT:
        return "INIT";
    case SCM_MSG_DATA:
        return "DATA";
    case SCM_MSG_SEEK:
        return "SEEK";
    case SCM_MSG_RELE:
        return "RELE";
    }
    return "NONE";
}

static int
_s2c(const char* s)
{
    if (strncmp(s, "INIT ", 5) == 0) {
        return SCM_MSG_INIT;
    } else if (strncmp(s, "DATA ", 5) == 0) {
        return SCM_MSG_DATA;
    } else if (strncmp(s, "SEEK ", 5) == 0) {
        return SCM_MSG_SEEK;
    } else if (strncmp(s, "RELE ", 5) == 0) {
        return SCM_MSG_RELE;
    } else {
        return -1;
    }
}

/////

void*
sc_message_pack(sc_message* msg)
{
    char* p;
    int n;
    size_t nd, total;

    nd = strlen(msg->displayName);
    total = 5 + nd + 2 + 12 + msg->length + 2;

    p = malloc(total);
    if (!p) {
        return NULL;
    }

    n = snprintf(p, total, "%s %s\r\n%d\r\n", _c2s(msg->code), msg->displayName, msg->length);
    if (n < 0) {
        free(p);
        return NULL;
    }

    if (msg->length > 0) {
        memcpy(p + n, msg->body, msg->length);
    }
    memcpy(p + n + msg->length, "\r\n", 2);

    return p;
}

int
sc_message_unpack(sc_message* msg, const char* data[3])
{
    const char *p0 = data[0], *p1 = p0 + 5, *px = NULL;

    px = p0 + strlen(p0) - 1;
    while (px > p0 && (*(px - 1) == '\r' || *(px - 1) == '\n')) {
        px--;
    }

    msg->code = _s2c(p0);
    memcpy(msg->displayName, p1, px - p1);
    msg->displayName[px - p1] = '\0';
    msg->length = strtoul(data[1], NULL, 10);
    msg->body = data[2];

    return 0;
}
