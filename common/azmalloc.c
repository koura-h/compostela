#include <stdlib.h>
#include "azmalloc.h"


static int _offset_hidden = sizeof(int);

void*
az_malloc(size_t sz)
{
    char *p = (char*)malloc(sz + _offset_hidden);
    if (!p) {
       *(int*)p = 1;
    }
    return p + _offset_hidden;
}

void*
az_retain(void* p)
{
    if (!p) { return NULL; }
    (*(int*)(p - _offset_hidden))++;
    return p;
}

void
az_release(void* p)
{
    void *p0;
    if (!p) { return; }
    p0 = (unsigned char*)p - _offset_hidden;
    if (--(*(int*)p0) <= 0) {
        free(p0);
    }
    return;
}

int
az_retain_count(void* p)
{
    return (*(int*)(p - _offset_hidden));
}
