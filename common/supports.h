#if !defined(__SUPPORTS_H__)
#define __SUPPORTS_H__

#if defined(__cplusplus)
extern "C" {
#endif

char*
strdup_pathcat(const char* p0, const char* p1);

char*
pathcat(const char* p0, ...);

int
sendall(int s, const void* data, ssize_t len, int opt);

int
recvall(int s, void* buf, ssize_t size, int opt);

int
set_non_blocking(int s);

int
mhash_with_size(const char* fpath, off_t fsize, unsigned char** mhash, size_t* mhash_size);

int
dump_mhash(const unsigned char* mhash, size_t mhash_size);

int
set_sigpipe_handler();

size_t
__w3cdatetime(char* buf, size_t sz, time_t t);

#if defined(__cplusplus)
}
#endif

#endif
