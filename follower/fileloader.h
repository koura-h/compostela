#if !defined(__FILELOADER_H__)
#define __FILELOADER_H__


typedef struct _sc_file_loader {
    struct _sc_file_loader _next;
    //
    void (*destroy_func)(void *);
    //
    char* displayName;
    int rotate;
    int append_timestamp;
} sc_file_loader;

typedef struct {
    sc_file_loader _parent;
    //
    char* path;
} sc_path_pattern_file_loader;

typedef struct {
    sc_file_loader _parent;
    //
    int fd;
} sc_logger_file_loader;


#endif
