#if !defined(__SCLOG_H__)
#define __SCLOG_H__

#include <stdarg.h>
#include <syslog.h>

#if 1
#define sc_log(lv, fmt, ...) \
	_sc_log(__FILE__, __LINE__, lv, fmt, ##__VA_ARGS__)
#else
#define sc_log(lv, fmt, ...) 
#endif

int _sc_log(const char* file, int line, int lv, const char* fmt, ...);

#endif
