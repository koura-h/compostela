/* $Id$ */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <byteswap.h>

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
_sc_follow_context_init(sc_follow_context* cxt, const char* dispname, int ftimestamp, size_t bufsize, sc_aggregator_connection_ref conn)
{
    memset(cxt, 0, sizeof(sc_follow_context));

    cxt->connection = conn;
    cxt->_fd = -1;

    if (dispname) {
        cxt->displayName = strdup(dispname);
        if (!cxt->displayName) {
            free(cxt->filename);
            free(cxt);
            return NULL;
        }
    }
    // we should read control files for 'fname'

    cxt->ftimestamp = ftimestamp;

    cxt->buffer = az_buffer_new(bufsize);
    cxt->message_buffer = sc_log_message_new(bufsize);
    cxt->message_buffer->code = htons(SCM_MSG_NONE);

    return cxt;
}

sc_follow_context*
sc_follow_context_new(const char* fname, const char* dispname, int ftimestamp, size_t bufsize, sc_aggregator_connection_ref conn)
{
    sc_follow_context* cxt = NULL;

    cxt = (sc_follow_context*)malloc(sizeof(sc_follow_context));
    if (cxt = _sc_follow_context_init(cxt, dispname, ftimestamp, bufsize, conn)) {
        if (fname) {
            cxt->filename = strdup(fname);
	    if (!cxt->filename) {
                sc_follow_context_destroy(cxt);
                return NULL;
	    }
        }
    }

    return cxt;
}

sc_follow_context*
sc_follow_context_new_with_fd(int fd, const char* dispname, int ftimestamp, size_t bufsize, sc_aggregator_connection_ref conn)
{
    sc_follow_context* cxt = NULL;

    cxt = (sc_follow_context*)malloc(sizeof(sc_follow_context));
    if (cxt = _sc_follow_context_init(cxt, dispname, ftimestamp, bufsize, conn)) {
        cxt->_fd = fd;
    }

    return cxt;
}

int
sc_follow_context_open_file(sc_follow_context* cxt)
{
    struct stat st;
    int flags;

    if (cxt->_fd != -1) {
        az_log(LOG_DEBUG, "already opened.");
        return -1;
    }

    cxt->_fd = open(cxt->filename, O_RDONLY | O_NONBLOCK);
    if (cxt->_fd < 0) {
        az_log(LOG_DEBUG, ">>> %s: error (%d)", __FUNCTION__, errno);
        return -1;
    }

    fstat(cxt->_fd, &st);
    cxt->filesize = st.st_size;
    cxt->mode = st.st_mode;

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
    cxt->filesize = 0;

    return 0;
}

void
sc_follow_context_reset(sc_follow_context* cxt)
{
    sc_follow_context_close_file(cxt);
    az_buffer_reset(cxt->buffer);
    cxt->message_buffer->code = htons(SCM_MSG_NONE);
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
    msg->code    = htons(SCM_MSG_RELE);
    msg->channel = htons(cxt->channel);
    msg->length  = htonl(sizeof(int32_t));
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

