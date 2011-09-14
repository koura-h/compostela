#if !defined(__APPCONFIG_H__)
#define __APPCONFIG_H__

typedef struct _sc_config_channel_entry {
    char* name;
    char* path;
    // char* displayName;
    int rotate;
    int append_timestamp;
} sc_config_channel_entry;

#include "azlist.h"

extern char   * g_config_server_address;
extern int      g_config_server_port;
extern int      g_config_waiting_seconds;
extern char   * g_config_control_path;

extern az_list* g_config_channels;

extern int load_config_file(const char* fname);
extern void clean_config();

#endif
