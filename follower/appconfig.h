#if !defined(__APPCONFIG_H__)
#define __APPCONFIG_H__

typedef struct _sc_config_pattern_entry {
    char* path;
    char* displayName;
    int rotate;
    //
    struct _sc_config_pattern_entry *_next;
} sc_config_pattern_entry;

extern char                   * g_config_server_address;
extern int                      g_config_server_port;
extern sc_config_pattern_entry* g_config_patterns;
extern int                      g_config_waiting_seconds;

extern int load_config_file(const char* fname);
extern void clean_config();

#endif
