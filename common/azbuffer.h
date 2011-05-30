#if !defined(__AZ_BUFFER_H__)
#define __AZ_BUFFER_H__

typedef struct _az_buffer {
    char* buffer;
    char* cursor;
    size_t size;
    size_t used;
} az_buffer;

az_buffer* az_buffer_new(size_t size);
void az_buffer_destroy(az_buffer* buf);

int az_buffer_read(az_buffer* buf, size_t len, char* dst, size_t dstsize);
ssize_t az_buffer_unread_bytes(az_buffer* buf);
int az_buffer_read_line(az_buffer* buf, char* dst, size_t dsize, size_t* dused);


int az_buffer_resize(az_buffer* buf, size_t newsize);

ssize_t az_buffer_fetch_bytes(az_buffer* buf, const void* data, size_t len);
ssize_t az_buffer_fetch_file(az_buffer* buf, int fd, size_t size);

int az_buffer_push_back(az_buffer* buf, const char* src, size_t ssize);

void az_buffer_reset(az_buffer* buf);


#endif
