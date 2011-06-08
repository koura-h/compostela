#if !defined(__APPCONFIG_H__)
#define __APPCONFIG_H__

#include "azlist.h"


typedef struct _sc_aggregate_context {
    char *path;
    char *displayName;
    //
    int f_rotate;
    int f_timestamp;
    int f_separate;
    int f_merge;
} sc_aggregate_context;


extern char*    g_config_server_logdir;
extern char*    g_config_server_addr;
extern int      g_config_server_port;

extern int      g_config_hostname_lookups;

extern az_list* g_config_aggregate_context_list;

extern int load_config_file(const char* fname);
extern void clean_config();

#endif
