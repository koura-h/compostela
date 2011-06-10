#if !defined(__AZ_BUFFER_H__)
#define __AZ_BUFFER_H__



typedef struct _az_buffer *az_buffer_ref;

#if defined(__cplusplus)
extern "C" {
#endif

az_buffer_ref az_buffer_new(size_t size);
void az_buffer_destroy(az_buffer_ref buf);

int az_buffer_read(az_buffer_ref buf, size_t len, char* dst, size_t dstsize);
ssize_t az_buffer_unread_bytes(az_buffer_ref buf);
int az_buffer_read_line(az_buffer_ref buf, char* dst, size_t dsize, size_t* dused, int *error);


int az_buffer_resize(az_buffer_ref buf, size_t newsize);

ssize_t az_buffer_fetch_bytes(az_buffer_ref buf, const void* data, size_t len);
ssize_t az_buffer_fetch_file(az_buffer_ref buf, int fd, size_t size);

int az_buffer_push_back(az_buffer_ref buf, const char* src, size_t ssize);

void az_buffer_reset(az_buffer_ref buf);

void* az_buffer_pointer(az_buffer_ref buf);
void* az_buffer_current(az_buffer_ref buf);
size_t az_buffer_size(az_buffer_ref buf);

#if defined(__cplusplus)
}
#endif


#endif
