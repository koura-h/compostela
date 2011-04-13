#if !defined(__APPCONFIG_H__)
#define __APPCONFIG_H__

typedef struct _sc_config_pattern_entry {
    const char* path;
    const char* displayName;
    int rotate;
    //
    struct _sc_config_pattern_entry *_next;
} sc_config_pattern_entry;

extern int g_config_listen_port;
extern sc_config_pattern_entry *g_config_patterns;

extern int parse_config_file(const char* fname);

#endif
