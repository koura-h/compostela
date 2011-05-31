#if !defined(__APPCONFIG_H__)
#define __APPCONFIG_H__

extern char *g_config_server_logdir;
extern char *g_config_server_addr;
extern int   g_config_server_port;

extern int   g_config_hostname_lookups;

extern int load_config_file(const char* fname);
extern void clean_config();

#endif
