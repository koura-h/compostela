/* $Id$ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "azbuffer.h"
#include "azlog.h"


struct _az_buffer {
    char* buffer;
    char* cursor;
    size_t size;
    size_t used;
};

az_buffer_ref
az_buffer_new(size_t size)
{
    az_buffer_ref buf = (az_buffer_ref)malloc(sizeof(struct _az_buffer));
    if (buf) {
        buf->buffer = malloc(size);
        buf->cursor = buf->buffer;
        buf->used = 0;
        buf->size = size;
    }
    return buf;
}

void
az_buffer_destroy(az_buffer_ref buf)
{
    free(buf->buffer);
    free(buf);
}

int
az_buffer_read(az_buffer_ref buf, size_t len, char* dst, size_t dsize)
{
    if (dsize < len) {
        // insufficient buffer size
        return -1;
    }
    if (az_buffer_unread_bytes(buf) < len) {
        // less data to read
        return -2;
    }

    memcpy(dst, buf->cursor, len);
    buf->cursor += len;
    return len;
}

ssize_t
az_buffer_unused_bytes(az_buffer_ref buf)
{
    return buf->size - buf->used;
}

ssize_t
az_buffer_unread_bytes(az_buffer_ref buf)
{
    return buf->buffer + buf->used - buf->cursor;
}

int
az_buffer_resize(az_buffer_ref buf, size_t newsize)
{
    size_t n = buf->cursor - buf->buffer;

    if (newsize < buf->used) {
        // shrink: not implemented yet
        return -1000;
    }

    buf->buffer = realloc(buf->buffer, newsize);
    if (buf->buffer) {
        buf->size = newsize;
        buf->cursor = buf->buffer + n;
    } else {
        buf->size = 0;
        buf->cursor = NULL;
        // error.
        return -1;
    }

    return 0;
}

ssize_t
az_buffer_fetch_bytes(az_buffer_ref buf, const void* data, size_t len)
{
    size_t unused = az_buffer_unused_bytes(buf);

    if (unused > len) {
        unused = len;
    }

    memcpy(buf->buffer + buf->used, data, unused);
    buf->used += unused;
    return unused;
}

ssize_t
az_buffer_fetch_file(az_buffer_ref buf, int fd, size_t size)
{
    ssize_t cb;
    size_t unused = az_buffer_unused_bytes(buf);

    if (unused > size) {
        unused = size;
    }

    cb = read(fd, buf->buffer + buf->used, unused);
    if (cb > 0) {
        buf->used += cb;
    }
    return cb;
}

int
az_buffer_read_line(az_buffer_ref buf, char* dst, size_t dsize, size_t* dused, int *error)
{
    char* p;
    size_t len;

    assert(dst != NULL);
    assert(dused != NULL);

    *dused = 0;

    len = az_buffer_unread_bytes(buf);
    if (len <= 0) {
        // we should fetch new data from buffering source
        return -1;
    }

    if (len > dsize) {
        len = dsize;
    }

    p = memchr(buf->cursor, '\n', len);
    if (p) {
        len = p - buf->cursor + 1;
    }

    az_buffer_read(buf, len, dst, dsize);
    if (len < dsize) {
        dst[len] = '\0';
    }
    *dused = len;

    return (p ? 1 : 0);
}

int
az_buffer_push_back(az_buffer_ref buf, const char* src, size_t ssize)
{
    // assert(buf->cursor == buf->buffer);
    size_t n, r = 0, u = 0;

    if (!src || ssize <= 0) {
        return 0;
    }

    n = buf->cursor - buf->buffer;
    u = az_buffer_unread_bytes(buf);
    if (ssize + u > buf->size) { // pushback + unread
        az_buffer_resize(buf, buf->size + ssize * 2); // adhoc
    }

    if (buf->used > 0) {
        r = (ssize > n ? ssize - n : 0);
        memmove(buf->cursor + r, buf->cursor, buf->used);
    }
    buf->cursor -= (n > ssize - r ? ssize - r : n);
    assert(buf->cursor >= buf->buffer);
    memcpy(buf->cursor, src, ssize);
    buf->used = u + (ssize > n ? ssize : n);

    return 0;
}

void
az_buffer_reset(az_buffer_ref buf)
{
    buf->cursor = buf->buffer;
    buf->used = 0;
}

void*
az_buffer_pointer(az_buffer_ref buf)
{
    return buf->buffer;
}

void*
az_buffer_current(az_buffer_ref buf)
{
    return buf->cursor;
}

size_t
az_buffer_size(az_buffer_ref buf)
{
    return buf->size;
}
