#include "fileloader.h"

sc_path_pattern_file_loader*
sc_path_pattern_file_loader_new()
{
    return NULL;
}

void
sc_file_loader_destroy(void* loader)
{
    sc_file_loader *ldr = loader;
    if (ldr && ldr->destroy_func) {
        ldr->destroy_func(ldr);
    }
}
