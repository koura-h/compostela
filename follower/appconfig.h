#if !defined(__APPCONFIG_H__)
#define __APPCONFIG_H__

typedef struct _sc_config_pattern_entry {
    char* path;
    char* displayName;
    int rotate;
    int append_timestamp;
} sc_config_pattern_entry;

extern char                   * g_config_server_address;
extern int                      g_config_server_port;
extern int                      g_config_waiting_seconds;
extern char                   * g_config_control_path;

extern sc_config_pattern_entry* g_config_patterns;

extern int load_config_file(const char* fname);
extern void clean_config();

#endif
