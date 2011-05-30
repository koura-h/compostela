#if !defined(__AZLOG_H__)
#define __AZLOG_H__

#include <stdarg.h>
#include <stddef.h>
#include <syslog.h>

#if 1
#define az_log(lv, fmt, ...) \
	_az_log(__FILE__, __LINE__, lv, fmt, ##__VA_ARGS__)
#else
#define az_log(lv, fmt, ...) 
#endif

int _az_log(const char* file, int line, int lv, const char* fmt, ...);

#endif
