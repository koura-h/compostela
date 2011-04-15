#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "azbuffer.h"

az_buffer*
az_buffer_new(size_t size)
{
    az_buffer* buf = (az_buffer*)malloc(sizeof(az_buffer));
    if (buf) {
        buf->buffer = malloc(size);
        buf->cursor = buf->buffer;
        buf->used = 0;
        buf->size = size;
    }
    return buf;
}

void
az_buffer_destroy(az_buffer* buf)
{
    free(buf->buffer);
    free(buf);
}

int
az_buffer_read(az_buffer* buf, size_t len, char* dst, size_t dsize)
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
az_buffer_unused_bytes(az_buffer* buf)
{
    return buf->size - buf->used;
}

ssize_t
az_buffer_unread_bytes(az_buffer* buf)
{
    return buf->buffer + buf->used - buf->cursor;
}

int
az_buffer_resize(az_buffer* buf, size_t newsize)
{
    size_t n = buf->cursor - buf->buffer;

    if (newsize > buf->used) {
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
az_buffer_fetch_file(az_buffer* buf, int fd, size_t size)
{
    ssize_t cb;
    size_t unused = buf->size - buf->used;

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
az_buffer_read_line(az_buffer* buf, char* dst, size_t dsize, size_t* dused)
{
    char* p, *ret;
    size_t len;

    len = az_buffer_unread_bytes(buf);
    if (len <= 0) {
        // we should fetch new data from buffering source
        return 1;
    }

    p = memchr(buf->cursor, '\n', len);
    if (p) {
        len = p - buf->cursor + 1;
    }

    if (len < dsize) {
        az_buffer_read(buf, len, dst, dsize);
        dst[len] = '\0';
        *dused = len;
        return (p ? 0 : 2);
    } else {
        // requiring efficient space for reading
        return -101;
    }
}


int
az_buffer_push_back(az_buffer* buf, const char* src, size_t ssize)
{
    assert(buf->cursor == buf->buffer);

    if (ssize + buf->used > buf->size) {
        fprintf(stderr, "%s(%s:%d) ssize = %d, buf->used = %d, buf->size = %d\n", __func__, __FILE__, __LINE__, ssize, buf->used, buf->size);
        return -1;
    }

    if (ssize > 0 && buf->used > 0) {
        memmove(buf->buffer + ssize, buf->buffer, buf->used);
    }
    memcpy(buf->buffer, src, ssize);

    return 0;
}

void
az_buffer_reset(az_buffer* buf)
{
    buf->cursor = buf->buffer;
    buf->used = 0;
}
