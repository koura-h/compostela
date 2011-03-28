/* $Id$ */
#include "azlist.h"
#include <string.h>
#include <stdlib.h>


az_list*
az_list_add(az_list* li0, void* object)
{
    az_list* list = (az_list*)malloc(sizeof(az_list));
    if (list) {
        memset(list, 0, sizeof(az_list));
        list->object = object;
    }

    list->next = li0;
    return list;
}

az_list*
az_list_delete(az_list* li, void *object)
{
    az_list* ret = NULL;
    if (li->object == object) {
        ret = li->next;
        free(li);

        return ret;
    } else {
        li->next = az_list_delete(li->next, object);
        return li;
    }
}

az_list*
az_list_delete_all(az_list* li)
{
    az_list *i = li, *i0;
    while (i) {
        i0 = i;
        i = i->next;
        free(i0);
    }
}

void
az_list_foreach(az_list* li, az_foreach_func func, void* data)
{
    az_list *i;
    for (i = li; i; i = i->next) {
        if (func(i->object, data) != 0) {
            break;
        }
    }
}

void*
az_list_find_ex(az_list* li, void* key, az_find_func func)
{
    az_list* i;
    for (i = li; i; i = i->next) {
        if (func(i->object, key)) {
            return i->object;
        }
    }

    return NULL;
}
