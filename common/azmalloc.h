#if !defined(__AZMALLOC_H__)
#define __AZMALLOC_H__

void* az_malloc(size_t n);
void* az_retain(void* p);
void  az_release(void* p);
int   az_retain_count(void* p);

#endif
