/* $Id$ */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <byteswap.h>
#include <glob.h>

#include "azlist.h"
#include "azbuffer.h"
#include "azlog.h"

#include "supports.h"

#include "message.h"
#include "connection.h"
#include "follow_context.h"

#include "appconfig.h"
#include "config.h"


/////

sc_follow_context*
_sc_follow_context_init(sc_follow_context* cxt, sc_config_channel_entry* config, size_t bufsize, sc_aggregator_connection_ref conn)
{
    memset(cxt, 0, sizeof(sc_follow_context));

    cxt->config = config;
    cxt->connection = conn;
    cxt->_fd = -1;

    cxt->displayName = strdup(cxt->config->name);

    cxt->buffer = az_buffer_new(bufsize);
    cxt->message_buffer = sc_log_message_new(bufsize);
    cxt->message_buffer->code = SCM_MSG_NONE;

    return cxt;
}

sc_follow_context*
sc_follow_context_new(sc_config_channel_entry* config, size_t bufsize, sc_aggregator_connection_ref conn)
{
    sc_follow_context* cxt = NULL;

    cxt = (sc_follow_context*)malloc(sizeof(sc_follow_context));
    if ((cxt = _sc_follow_context_init(cxt, config, bufsize, conn))) {
    }

    return cxt;
}

/*
sc_follow_context*
sc_follow_context_new_with_fd(int fd, const char* dispname, int ftimestamp, size_t bufsize, sc_aggregator_connection_ref conn)
{
    sc_follow_context* cxt = NULL;

    cxt = (sc_follow_context*)malloc(sizeof(sc_follow_context));
    if ((cxt = _sc_follow_context_init(cxt, config, bufsize, conn))) {
        cxt->_fd = fd;
    }

    return cxt;
}
*/


static char*
__pickup_first_file(sc_follow_context* cxt)
{
    char *fname = NULL;
    glob_t globbuf;
    int i;

    glob(cxt->config->path, 0, NULL, &globbuf);
    for (i = 0; i < globbuf.gl_pathc; i++) {
        fname = strdup(globbuf.gl_pathv[i]);
        break;
    }
    globfree(&globbuf);

    return fname;
}

int
sc_follow_context_open_file(sc_follow_context* cxt)
{
    struct stat st;
    char *fname;
    // int flags;

    if (cxt->_fd != -1) {
        az_log(LOG_DEBUG, "already opened.");
        return -1;
    }

    fname = __pickup_first_file(cxt);
    if (!fname) {
        az_log(LOG_DEBUG, "no files picked up.");
        return -1;
    }
    az_log(LOG_DEBUG, "fname = %s", fname);

    cxt->_fd = open(fname, O_RDONLY | O_NONBLOCK);
    if (cxt->_fd < 0) {
        free(fname);
        az_log(LOG_DEBUG, ">>> %s: error (%d)", __FUNCTION__, errno);
        return -1;
    }

    fstat(cxt->_fd, &st);
    // cxt->filesize = st.st_size;
    cxt->mode     = st.st_mode;
    cxt->filename = fname;

    return 0;
}

int
sc_follow_context_close_file(sc_follow_context* cxt)
{
    assert(cxt != NULL);
    if (cxt->_fd != -1) {
        close(cxt->_fd);
	cxt->_fd = -1;
    }

    cxt->mode = 0;
    // cxt->filesize = 0;
    cxt->position = 0;

    return 0;
}

void
sc_follow_context_reset(sc_follow_context* cxt)
{
    sc_follow_context_close_file(cxt);
    az_buffer_reset(cxt->buffer);
    cxt->message_buffer->code = SCM_MSG_NONE;
}

int
sc_follow_context_close(sc_follow_context* cxt)
{
    sc_log_message* msg = NULL, *resp = NULL;
    int ret;

    az_log(LOG_DEBUG, "context close");
    if (!sc_aggregator_connection_is_opened(cxt->connection)) {
        // disconnected. but show must go on.
	az_log(LOG_DEBUG, ">>> %s: PLEASE RECONNECT NOW!", __FUNCTION__);
	return 1001;
    }

    if (cxt->_fd < 0) {
        az_log(LOG_DEBUG, "already closed.");
	return -1;
    }

    msg = sc_log_message_new(sizeof(int32_t));
    msg->code    = SCM_MSG_RELE;
    msg->channel = cxt->channel;
    memset(msg->content, 0, sizeof(int32_t));

    if ((ret = sc_aggregator_connection_send_message(cxt->connection, msg)) != 0) {
	// connection broken
	az_log(LOG_DEBUG, "RELE: connection has broken.");
	sc_log_message_destroy(msg);
	return 1001;
    }

    if ((ret = sc_aggregator_connection_receive_message(cxt->connection, &resp)) != 0) {
	az_log(LOG_DEBUG, "RELE: connection has broken. (on receiving) = %d", ret);
	sc_log_message_destroy(msg);
	return 1001;
    }
    sc_log_message_destroy(msg);

    return 0;
}


void
sc_follow_context_destroy(sc_follow_context* cxt)
{
    sc_log_message_destroy(cxt->message_buffer);
    az_buffer_destroy(cxt->buffer);

    free(cxt->filename);
    free(cxt->displayName);
    free(cxt);
}

