#if !defined(__SUPPORTS_H__)
#define __SUPPORTS_H__

#if defined(__cplusplus)
extern "C" {
#endif

int
sendall(int s, const void* data, ssize_t len, int opt);

int
recvall(int s, void* buf, ssize_t size, int opt);

#if defined(__cplusplus)
}
#endif

#endif
