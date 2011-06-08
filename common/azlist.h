/* $Id$ */
#if !defined(__AZLIST_H__)
#define __AZLIST_H__


typedef struct _az_list {
    struct _az_list *next;
    //
    void *object;
} az_list;

typedef int (*az_foreach_func)(void*, void*);
typedef int (*az_find_func)(void*, void*);
typedef void (*az_delete_func)(void*);

az_list* az_list_new();

az_list* az_list_add(az_list* li, void* object);

az_list* az_list_delete(az_list* li, void *object);
az_list* az_list_delete_all(az_list* li);

az_list* az_list_reverse(az_list *li);

void az_list_foreach(az_list* li, az_foreach_func func, void* data);

void* az_list_find(az_list* li, void* key);
void* az_list_find_ex(az_list* li, void* key, az_find_func func);


#endif
